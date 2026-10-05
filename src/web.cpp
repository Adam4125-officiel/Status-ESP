// Web server.
//
//   GET  /                     the whole web interface (gzip from PROGMEM, web/index.html)
//   GET  /api/status           version, network, memory, flash, time, weather state
//   GET  /api/settings         every setting          POST /api/settings  partial update
//   GET  /api/wifi/scan        async scan             POST /api/wifi      save Wi-Fi, reboot
//   GET  /api/files?dir=       list /image or /gif    POST /api/upload?dir=   multipart
//   POST /api/delete           only directly inside /image/ or /gif/ (403 otherwise)
//   GET  /api/geocode?q=       city search (Open-Meteo)
//   POST /api/reboot           POST /api/factory-reset   (deletes only /custom.json)
//   GET  /update POST /update  firmware update (our page + the library's handler)
//   GET  /v.json  /reboot  /set?brt=&blinv=  /wifi   kept for backward compatibility
//   anything else              404, or a redirect to the home page in rescue-AP mode
#include "web.h"

#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "display.h"
#include "generated/web_index.h"
#include "geocode.h"
#include "media.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"
#include "weather.h"

namespace web {

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static bool listening = false;

// --- Helpers ------------------------------------------------------------------------

static void sendJson(int code, const JsonDocument &doc) {
  String out;
  out.reserve(256);
  serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(code, F("application/json"), out);
}

static void sendError(int code, const String &message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

static void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

// The JSON body of a POST (ESP8266WebServer exposes it as the "plain" argument).
// Replies 400 itself and returns false when there is none or it is unusable.
static bool readJsonBody(JsonDocument &doc) {
  if (!server.hasArg("plain")) {
    sendError(400, F("Missing JSON body"));
    return false;
  }
  const String &body = server.arg("plain");
  if (body.length() > config::MAX_JSON_BODY) {
    sendError(413, F("Request too large"));
    return false;
  }
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    sendError(400, F("Invalid JSON"));
    return false;
  }
  return true;
}

// "/image" or "/gif" (a trailing slash is tolerated), nullptr for anything else.
static const char *folderFromArg(const String &arg) {
  if (arg == config::DIR_IMAGE || arg == String(config::DIR_IMAGE) + "/") return config::DIR_IMAGE;
  if (arg == config::DIR_GIF || arg == String(config::DIR_GIF) + "/") return config::DIR_GIF;
  return nullptr;
}

static bool isGifFolder(const char *folder) { return strcmp(folder, config::DIR_GIF) == 0; }

static bool extensionAllowed(const char *folder, const char *name) {
  if (isGifFolder(folder)) return media::isGif(name);
  return media::isJpg(name) || media::isGif(name);
}

static void fsUsage(size_t &total, size_t &used) {
  FSInfo info;
  total = used = 0;
  if (settings::fsMounted() && LittleFS.info(info)) {
    total = info.totalBytes;
    used = info.usedBytes;
  }
}

// --- Pages ------------------------------------------------------------------------------

static void handleIndex() {
  server.sendHeader(F("Content-Encoding"), F("gzip"));
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send_P(200, PSTR("text/html"), (PGM_P)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

// /update page without the "FileSystem" form (which would erase the whole stock file
// area). Registered BEFORE updater.setup() so that it replaces the library's GET page;
// POST handling stays the library's proven one.
static void handleUpdatePage() {
  server.send(200, "text/html", F(
      "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Firmware update</title><style>body{font-family:sans-serif;max-width:560px;"
      "margin:auto;padding:16px;background:#111;color:#eee}input{margin:8px 0}</style>"
      "</head><body><h2>Firmware update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' accept='.bin,.bin.gz' name='firmware'><br>"
      "<input type='submit' value='Update'></form>"
      "<p>Do not cut the power during the update.</p>"
      "<p><a href='/' style='color:#8cf'>Back</a></p></body></html>"));
}

// --- API: status and settings -----------------------------------------------------------------

static void handleStatus() {
  JsonDocument doc;
  doc["name"] = FW_NAME;
  doc["version"] = FW_VERSION;
  doc["fw"] = FW_FULL_NAME;
  doc["ap"] = net::isAp();
  doc["ip"] = net::ip();
  if (net::isConnected()) {
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
  }
  doc["heap"] = ESP.getFreeHeap();
  doc["max_block"] = ESP.getMaxFreeBlockSize();
  doc["sketch"] = ESP.getSketchSize();
  uint32_t freeOta = ESP.getFreeSketchSpace();
  doc["free_ota"] = freeOta;
  doc["rollback_ok"] = freeOta >= config::STOCK_FW_SIZE;
  size_t total, used;
  fsUsage(total, used);
  doc["fs_mounted"] = settings::fsMounted();
  doc["fs_total"] = total;
  doc["fs_used"] = used;
  doc["uptime"] = millis() / 1000;

  struct tm t;
  if (timekeeping::localTime(t)) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    doc["time"] = String(buf);
  }
  doc["offset"] = timekeeping::offsetSeconds();
  doc["offset_known"] = timekeeping::offsetKnown();

  const weather::Data &w = weather::data();
  doc["weather_ok"] = w.valid;
  if (w.valid) doc["weather_age"] = (millis() - w.updatedAtMs) / 1000;
  sendJson(200, doc);
}

static void handleSettingsGet() {
  JsonDocument doc;
  settings::toJson(doc);
  sendJson(200, doc);
}

static void handleSettingsPost() {
  JsonDocument body;
  if (!readJsonBody(body)) return;
  uint32_t changed = settings::apply(body.as<JsonObjectConst>());
  // ?save=0 applies without writing flash: used by the live brightness slider, which
  // fires on every movement; the final value is saved when the slider is released.
  bool persist = !(server.hasArg("save") && server.arg("save") == "0");
  if (persist && !settings::save()) {
    sendError(500, F("Could not write the settings file"));
    return;
  }
  display::settingsChanged(changed);
  if (changed & settings::CH_NTP) timekeeping::applySettings();
  if (changed & settings::CH_LOCATION) weather::requestRefresh();

  JsonDocument doc;
  settings::toJson(doc);
  doc["ok"] = true;
  sendJson(200, doc);
}

// --- API: Wi-Fi -----------------------------------------------------------------------------------

static void handleWifiScan() {
  int status = net::scanStatus();
  if (server.hasArg("new") && status != WIFI_SCAN_RUNNING) {
    net::scanDone();
    net::scanStart();
    status = WIFI_SCAN_RUNNING;
  }
  if (status == WIFI_SCAN_FAILED) {  // nothing started yet
    net::scanStart();
    status = WIFI_SCAN_RUNNING;
  }
  JsonDocument doc;
  if (status == WIFI_SCAN_RUNNING) {
    doc["scanning"] = true;
    sendJson(200, doc);
    return;
  }

  // Finished: strongest first, hidden networks and repeated names dropped.
  const int MAX_NETWORKS = 24;
  uint8_t order[MAX_NETWORKS];
  int count = 0;
  for (int i = 0; i < status && count < MAX_NETWORKS; i++) {
    if (WiFi.SSID(i).length() == 0) continue;
    int j = count++;
    while (j > 0 && WiFi.RSSI(order[j - 1]) < WiFi.RSSI(i)) {
      order[j] = order[j - 1];
      j--;
    }
    order[j] = (uint8_t)i;
  }
  doc["scanning"] = false;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int k = 0; k < count; k++) {
    int i = order[k];
    String ssid = WiFi.SSID(i);
    bool duplicate = false;
    for (JsonObject o : list) {
      if (ssid == o["ssid"].as<const char *>()) duplicate = true;
    }
    if (duplicate) continue;
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  net::scanDone();
  sendJson(200, doc);
}

static void handleWifiSave() {
  JsonDocument body;
  String ssid, pass;
  if (server.hasArg("plain")) {
    if (!readJsonBody(body)) return;
    ssid = body["ssid"] | "";
    pass = body["pass"] | "";
  } else {
    ssid = server.arg("ssid");
    pass = server.arg("pass");
  }
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63) {
    sendError(400, F("Missing or too long SSID / password"));
    return;
  }
  sendOk();
  net::saveCredentialsAndReboot(ssid, pass);
}

// --- API: files ---------------------------------------------------------------------------------------

static void handleFiles() {
  const char *folder = folderFromArg(server.arg("dir"));
  if (!folder) {
    sendError(400, F("dir must be /image or /gif"));
    return;
  }
  size_t total, used;
  fsUsage(total, used);
  JsonDocument doc;
  doc["dir"] = folder;
  doc["total"] = total;
  doc["used"] = used;
  doc["reserve"] = config::FS_RESERVE_BYTES;
  JsonArray files = doc["files"].to<JsonArray>();
  if (settings::fsMounted()) {
    Dir d = LittleFS.openDir(folder);
    while (d.next() && files.size() < 200) {
      if (!d.isFile()) continue;
      String name = d.fileName();
      if (!extensionAllowed(folder, name.c_str())) continue;
      JsonObject o = files.add<JsonObject>();
      o["name"] = name;
      o["size"] = d.fileSize();
    }
  }
  sendJson(200, doc);
}

// Upload state: the multipart handler runs once per chunk, the final handler once at the end.
static File upFile;
static bool upFailed = false;
static int upCode = 200;
static String upMessage;
static String upName;
static String upPath;
static size_t upWritten = 0;
static size_t upLimit = 0;

static void upFail(int code, const char *message) {
  if (upFailed) return;
  upFailed = true;
  upCode = code;
  upMessage = message;
  if (upFile) upFile.close();
  if (upPath.length()) LittleFS.remove(upPath);
}

// Reduces what the browser sent to a safe leaf name: [A-Za-z0-9._-], no "..", at most
// 31 characters, a whitelisted extension in lower case. Empty = refuse.
static String sanitizeUploadName(const String &raw, const char *folder) {
  int slash = max(raw.lastIndexOf('/'), raw.lastIndexOf('\\'));
  String leaf = slash >= 0 ? raw.substring(slash + 1) : raw;
  int dot = leaf.lastIndexOf('.');
  if (dot < 0) return "";
  String ext = leaf.substring(dot);
  ext.toLowerCase();
  String base = leaf.substring(0, dot);
  String probe = String("x") + ext;
  if (!extensionAllowed(folder, probe.c_str())) return "";

  String clean;
  for (size_t i = 0; i < base.length(); i++) {
    char c = base[i];
    if (c == ' ') c = '_';
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.';
    if (!ok) continue;
    if (c == '.' && (clean.length() == 0 || clean[clean.length() - 1] == '.')) continue;  // no leading / double dot
    clean += c;
  }
  while (clean.length() && clean[clean.length() - 1] == '.') clean.remove(clean.length() - 1);
  size_t room = config::MAX_FILE_NAME - ext.length();
  if (clean.length() > room) clean = clean.substring(0, room);
  if (clean.isEmpty()) clean = "image";
  String name = clean + ext;
  return settings::validFileName(name.c_str()) ? name : String("");
}

static void handleUploadChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    upFailed = false;
    upCode = 200;
    upMessage = "";
    upName = "";
    upPath = "";
    upWritten = 0;
    if (upFile) upFile.close();

    const char *folder = folderFromArg(server.arg("dir"));
    if (!folder) return upFail(400, "dir must be /image or /gif");
    if (!settings::fsMounted()) return upFail(500, "Storage is not available");
    String name = sanitizeUploadName(up.filename, folder);
    if (name.isEmpty()) {
      return upFail(400, isGifFolder(folder) ? "Only .gif files can go in this folder"
                                                   : "Only .jpg, .jpeg and .gif files are accepted");
    }
    size_t total, used;
    fsUsage(total, used);
    size_t freeBytes = total > used ? total - used : 0;
    if (freeBytes <= config::FS_RESERVE_BYTES) return upFail(507, "Not enough free space");
    upLimit = freeBytes - config::FS_RESERVE_BYTES;
    if (up.contentLength > upLimit) return upFail(507, "Not enough free space for this file");

    upName = name;
    upPath = String(folder) + "/" + name;
    upFile = LittleFS.open(upPath, "w");
    if (!upFile) upFail(500, "Could not create the file");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (upFailed || !upFile) return;
    if (upWritten + up.currentSize > upLimit) return upFail(507, "Not enough free space for this file");
    size_t n = upFile.write(up.buf, up.currentSize);
    if (n != up.currentSize) return upFail(507, "Write failed: storage is full");
    upWritten += n;
  } else if (up.status == UPLOAD_FILE_END) {
    if (upFile) upFile.close();
    if (!upFailed && upWritten == 0) upFail(400, "The file is empty");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    upFail(400, "Upload interrupted");
  }
}

static void handleUploadDone() {
  if (upFile) upFile.close();
  if (upFailed) {
    sendError(upCode, upMessage);
  } else if (upName.isEmpty()) {
    sendError(400, F("No file received"));
  } else {
    JsonDocument doc;
    doc["ok"] = true;
    doc["name"] = upName;
    doc["size"] = upWritten;
    sendJson(200, doc);
  }
  upFailed = false;
  upName = "";
  upPath = "";
}

static void handleDelete() {
  String path;
  if (server.hasArg("plain")) {
    JsonDocument body;
    if (!readJsonBody(body)) return;
    path = body["path"] | "";
  } else {
    path = server.arg("path");
  }

  // Only a file directly inside /image/ or /gif/ may ever be deleted: everything else
  // (the stock files, /custom.json, other folders, "..") is refused.
  const char *folder = nullptr;
  if (path.startsWith(String(config::DIR_IMAGE) + "/")) folder = config::DIR_IMAGE;
  else if (path.startsWith(String(config::DIR_GIF) + "/")) folder = config::DIR_GIF;
  String leaf = folder ? path.substring(strlen(folder) + 1) : String("");
  bool clean = folder && leaf.length() > 0 && leaf.length() <= config::MAX_FILE_NAME && leaf.indexOf('/') < 0 &&
               leaf.indexOf('\\') < 0 && leaf.indexOf("..") < 0 && leaf[0] != '.';
  for (size_t i = 0; clean && i < leaf.length(); i++) {
    if ((uint8_t)leaf[i] < 32 || (uint8_t)leaf[i] > 126) clean = false;
  }
  if (!clean) {
    sendError(403, F("Only files directly inside /image/ or /gif/ can be deleted"));
    return;
  }
  File f = LittleFS.open(path, "r");
  bool isFile = f && !f.isDirectory();
  if (f) f.close();
  if (!isFile) {
    sendError(404, F("No such file"));
    return;
  }
  if (!LittleFS.remove(path)) {
    sendError(500, F("Could not delete the file"));
    return;
  }
  // A deleted file must not stay selected.
  settings::Settings &s = settings::get();
  uint32_t changed = 0;
  if (isGifFolder(folder) && leaf == s.weatherGif) {
    s.weatherGif[0] = '\0';
    changed |= settings::CH_WEATHER;
  }
  if (!isGifFolder(folder) && leaf == s.albumFile) {
    s.albumFile[0] = '\0';
    changed |= settings::CH_ALBUM;
  }
  if (changed) {
    settings::save();
    display::settingsChanged(changed);
  }
  sendOk();
}

// --- API: geocoding and system actions ----------------------------------------------------------------

static void handleGeocode() {
  String q = server.arg("q");
  q.trim();
  if (q.length() < 2 || q.length() > 60) {
    sendError(400, F("Type at least 2 characters"));
    return;
  }
  JsonDocument doc;
  String error;
  if (!geocode::search(q.c_str(), doc, error)) {
    sendError(502, error);
    return;
  }
  sendJson(200, doc);
}

static void handleReboot() {
  sendOk();
  delay(300);
  ESP.restart();
}

static void handleFactoryReset() {
  bool ok = settings::factoryReset();  // deletes /custom.json only
  if (!ok) {
    sendError(500, F("Could not delete the settings file"));
    return;
  }
  sendOk();
  delay(300);
  ESP.restart();
}

// --- Backward compatibility (0.1.0 - 0.2.0 interface) ---------------------------------------------------------

static void handleLegacySet() {
  settings::Settings &s = settings::get();
  uint32_t changed = 0;
  if (server.hasArg("brt")) {
    s.brightness = (uint8_t)constrain(server.arg("brt").toInt(), 0, 100);
    changed |= settings::CH_BRIGHTNESS;
  }
  if (server.hasArg("blinv")) {
    s.blInverted = !s.blInverted;
    changed |= settings::CH_BRIGHTNESS;
  }
  display::settingsChanged(changed);
  settings::save();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleLegacyWifiPage() {
  String h = F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>Wi-Fi</title><style>body{font-family:sans-serif;max-width:560px;margin:auto;"
               "padding:16px;background:#111;color:#eee}input,select,button{width:100%;padding:10px;"
               "margin:6px 0;box-sizing:border-box}</style></head><body><h2>Wi-Fi network</h2>"
               "<form method='POST' action='/wifi'><select onchange=\"s.value=this.value\">"
               "<option value=''>-- detected networks --</option>");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.replace("&", "&amp;");
    ssid.replace("'", "&#39;");
    ssid.replace("<", "&lt;");
    h += "<option value='" + ssid + "'>" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();
  h += F("</select><input id='s' name='ssid' placeholder='Network name (SSID)' required>"
         "<input name='pass' type='password' placeholder='Password'>"
         "<button>Save and reboot</button></form>"
         "<p><a href='/' style='color:#8cf'>Back</a> - <a href='/update' style='color:#8cf'>Firmware update</a></p>"
         "</body></html>");
  server.send(200, "text/html", h);
}

static void handleLegacyWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.isEmpty()) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }
  server.send(200, "text/html",
              "<meta charset='utf-8'>Saved. Rebooting... If the connection fails, "
              "the " FW_NAME " network will reappear.");
  net::saveCredentialsAndReboot(ssid, pass);
}

static void handleLegacyReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(300);
  ESP.restart();
}

static void handleVersion() {
  server.send(200, "application/json", "{\"m\":\"SmallTV-Ultra\",\"v\":\"" FW_FULL_NAME "\"}");
}

static void handleNotFound() {
  if (net::isAp()) {  // simple captive portal: everything redirects to the home page
    server.sendHeader("Location", String("http://") + config::AP_IP + "/");
    server.send(302);
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// --- Setup / loop --------------------------------------------------------------------------------------------------

void begin() {
  // Must stay before updater.setup(): see handleUpdatePage().
  server.on("/update", HTTP_GET, handleUpdatePage);
  updater.setup(&server, "/update");

  server.on("/", HTTP_GET, handleIndex);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/files", HTTP_GET, handleFiles);
  server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/geocode", HTTP_GET, handleGeocode);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);

  server.on("/set", handleLegacySet);
  server.on("/wifi", HTTP_GET, handleLegacyWifiPage);
  server.on("/wifi", HTTP_POST, handleLegacyWifiSave);
  server.on("/reboot", handleLegacyReboot);
  server.on("/v.json", handleVersion);
  server.onNotFound(handleNotFound);
}

void loop() {
  if (!listening) {
    // The same moment the 0.1.0 firmware chose: once the Wi-Fi mode is settled.
    if (!net::isConnected() && !net::isAp()) return;
    server.begin();
    listening = true;
  }
  server.handleClient();
}

}  // namespace web
