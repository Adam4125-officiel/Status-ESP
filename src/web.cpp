// Web server.
//
//   GET  /                     the whole web interface (gzip from PROGMEM, web/index.html)
//   GET  /api/status           version, network, memory, flash, time, weather state
//   GET  /api/settings         every setting          POST /api/settings  partial update
//   GET  /api/settings/export  the settings as a file POST /api/settings/import  restore one
//   GET  /api/wifi/scan        async scan             POST /api/wifi      save Wi-Fi, reboot
//   GET  /api/files?dir=       list /image or /gif    POST /api/upload?dir=   multipart
//   POST /api/delete           only directly inside /image/ or /gif/ (403 otherwise)
//   GET  /api/geocode?q=       city search (Open-Meteo)
//   POST /api/weather/refresh  fetch the weather again now
//   POST /api/portal/refresh   ask Status-Portal again now ("Test connection")
//   GET  /api/backup           every stored file as one tar (Wi-Fi password included; not in rescue mode)
//   POST /api/backup/restore   put such a tar back (multipart), then reboot
//   POST /api/reboot           POST /api/factory-reset   (deletes only /custom.json)
//   GET  /update POST /update  firmware update (our page + the library's handler)
//   GET  /v.json  /reboot  /set?brt=&blinv=  /wifi   kept for backward compatibility
//   anything else              404, or a redirect to the home page in rescue-AP mode
#include "web.h"

#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "backup.h"
#include "config.h"
#include "display.h"
#include "generated/web_index.h"
#include "geocode.h"
#include "mdns.h"
#include "media.h"
#include "net.h"
#include "portal.h"
#include "portal_url.h"
#include "settings.h"
#include "timekeeping.h"
#include "weather.h"

namespace web {

static ESP8266WebServer server(80);

// What the hook in begin() saw when the current request started: the free heap, and whether a GIF was
// playing (0 no, 1 yes and it stayed, 2 yes and it was closed to make room). Shown by /api/status so that
// "why does my GIF stop" can be answered from the web interface.
static uint32_t requestHeap;
static uint8_t requestGif;
static ESP8266HTTPUpdateServer updater;
static bool listening = false;

// --- Password (HTTP Basic, user "admin") ------------------------------------------------
// Off unless a password is set. NEVER enforced in rescue access-point mode: /update (and the
// rest of the interface) must stay reachable there, it is the only way back from a device
// that has lost its Wi-Fi, a forgotten password included. /v.json and the 404 / captive
// portal answers are always open.

static const char *const AUTH_USER = "admin";

static bool authEnforced() { return settings::get().password[0] != '\0' && !net::isAp(); }

static bool authorized() { return !authEnforced() || server.authenticate(AUTH_USER, settings::get().password); }

// True when the request may go on; otherwise has already sent the 401 (the browser then asks).
static bool guard() {
  if (authorized()) return true;
  server.requestAuthentication(BASIC_AUTH, "Status-ESP", F("Password required"));
  return false;
}

// POST /update is the library's handler: it has its own credentials, which follow the same rule.
static void applyUpdaterCredentials() {
  updater.updateCredentials(authEnforced() ? String(AUTH_USER) : String(),
                            authEnforced() ? String(settings::get().password) : String());
}

// Registers a route behind guard().
typedef void (*Handler)();
static void route(const char *uri, HTTPMethod method, Handler fn) {
  server.on(uri, method, [fn]() {
    if (guard()) fn();
  });
}

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
  doc["auth"] = settings::get().password[0] != '\0';
  doc["auth_enforced"] = authEnforced();
  if (mdns::active()) doc["mdns"] = mdns::name();
  if (net::isConnected()) {
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
  }
  doc["heap"] = ESP.getFreeHeap();
  doc["req_heap"] = requestHeap;
  doc["gif"] = requestGif;
  if (media::lastError()[0]) doc["media_err"] = media::lastError();
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
  const weather::Diag &wd = weather::diag();   // why the weather is not arriving (empty when it is)
  if (wd.error[0]) doc["weather_err"] = wd.error;
  if (wd.attemptMs) {
    doc["weather_try_age"] = (millis() - wd.attemptMs) / 1000;
    doc["weather_fails"] = wd.failures;
    doc["weather_http"] = wd.httpCode;
  }
  doc["portal_on"] = portal::configured();
  if (portal::configured()) {   // why Status-Portal is not answering (empty when it is); never the address or the key
    doc["portal_ok"] = portal::fresh();
    if (portal::updatedAtMs()) {
      doc["portal_age"] = (millis() - portal::updatedAtMs()) / 1000;
      doc["portal_overall"] = portal::statusName(portal::data().overall);
    }
    const portal::Diag &pd = portal::diag();
    if (pd.error[0]) doc["portal_err"] = pd.error;
    if (pd.attemptMs) {
      doc["portal_try_age"] = (millis() - pd.attemptMs) / 1000;
      doc["portal_fails"] = pd.failures;
      doc["portal_http"] = pd.httpCode;
    }
  }
  sendJson(200, doc);
}

// The settings as the API returns them: never the password or the Status-Portal key themselves, only
// whether one is set.
static void settingsToJson(JsonDocument &doc) {
  settings::toJson(doc);
  doc["pw_set"] = settings::get().password[0] != '\0';
  doc["portal_key_set"] = settings::get().portalKey[0] != '\0';
}

static void handleSettingsGet() {
  JsonDocument doc;
  settingsToJson(doc);
  sendJson(200, doc);
}

// Applies a settings object (already validated by settings::apply()), optionally writes it
// to /custom.json, applies it live and answers with the resulting settings.
static void applySettingsAndReply(JsonDocument &body, bool persist) {
  uint32_t changed = settings::apply(body.as<JsonObjectConst>());
  if (persist && !settings::save()) {
    sendError(500, F("Could not write the settings file"));
    return;
  }
  display::settingsChanged(changed);
  if (changed & settings::CH_NTP) timekeeping::applySettings();
  if (changed & settings::CH_LOCATION) weather::requestRefresh();
  if (changed & settings::CH_AUTH) applyUpdaterCredentials();
  if (changed & settings::CH_PORTAL) portal::settingsChanged();

  JsonDocument doc;
  settingsToJson(doc);
  doc["ok"] = true;
  sendJson(200, doc);
}

// The Status-Portal address and key are refused with a sentence, not ignored: a typo that vanishes
// silently looks exactly like a save that worked. Replies 400 itself and returns false when refused.
static bool checkPortalFields(JsonDocument &body) {
  if (body["portal_url"].is<const char *>()) {
    const char *typed = body["portal_url"].as<const char *>();
    char normal[portal::MAX_URL + 1];
    if (typed[0] != '\0') {
      switch (portal::checkUrl(typed, normal, sizeof(normal))) {
        case portal::URL_OK: break;
        case portal::URL_HTTPS:
          sendError(400, F("https cannot work, the device has no TLS: use the portal's http:// address"));
          return false;
        case portal::URL_SCHEME:
          sendError(400, F("Only http:// addresses work"));
          return false;
        case portal::URL_CREDENTIALS:
          sendError(400, F("No user name or password in the address: the key is separate"));
          return false;
        case portal::URL_PATH:
          sendError(400, F("Give only the address and port, without a path"));
          return false;
        case portal::URL_PORT:
          sendError(400, F("The port must be a number from 1 to 65535"));
          return false;
        default:
          sendError(400, F("Not a usable address: use http://<ip address>:<port>"));
          return false;
      }
    }
  }
  if (body["portal_key"].is<const char *>()) {
    const char *key = body["portal_key"].as<const char *>();
    if (key[0] != '\0' && !portal::validKey(key)) {
      sendError(400, F("The key must be 8 to 64 characters without spaces"));
      return false;
    }
  }
  return true;
}

static void handleSettingsPost() {
  JsonDocument body;
  if (!readJsonBody(body)) return;
  if (body["pw"].is<const char *>()) {
    const char *pw = body["pw"].as<const char *>();
    if (pw[0] != '\0' && !settings::validPassword(pw)) {
      sendError(400, F("The password must be 4 to 32 printable characters"));
      return;
    }
  }
  if (!checkPortalFields(body)) return;
  // ?save=0 applies without writing flash: used by the live brightness slider, which
  // fires on every movement; the final value is saved when the slider is released.
  bool persist = !(server.hasArg("save") && server.arg("save") == "0");
  applySettingsAndReply(body, persist);
}

// The settings as a downloadable file: the same JSON /custom.json holds.
static void handleSettingsExport() {
  JsonDocument doc;
  settings::toJson(doc);
  String out;
  out.reserve(1024);
  serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"status-esp-settings.json\""));
  server.send(200, F("application/json"), out);
}

// Restores a file made by the export. Same validation as POST /api/settings (it is the same
// settings::apply()), and refused when the file has no setting in it at all, so that picking
// the wrong file does not look like a successful import.
static void handleSettingsImport() {
  JsonDocument body;
  if (!readJsonBody(body)) return;
  body.remove("pw");        // an import never touches the password, in either direction
  body.remove("pw_set");
  body.remove("portal_key");   // nor the Status-Portal key (an export never has it either)
  body.remove("portal_key_set");
  if (!checkPortalFields(body)) return;
  JsonDocument known;
  settings::toJson(known);
  size_t recognised = 0;
  for (JsonPair kv : body.as<JsonObject>()) {
    if (!known[kv.key()].isNull()) recognised++;
  }
  if (recognised == 0) {
    sendError(400, F("This does not look like a Status-ESP settings file"));
    return;
  }
  applySettingsAndReply(body, true);
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

    if (!authorized()) return upFail(401, "Password required");
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
  if (upFailed && upCode == 401) {   // refused at the start: nothing was written
    upFailed = false;
    guard();
    return;
  }
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

// --- API: full backup and restore ---------------------------------------------------------------------

// Sends one piece of the archive; false once the browser has gone away.
static bool backupSink(void *ctx, const uint8_t *data, size_t len) {
  WiFiClient *client = static_cast<WiFiClient *>(ctx);
  while (len) {
    if (!client->connected()) return false;
    size_t n = client->write(data, len);   // waits for room in the send buffer (a few seconds at most)
    if (n == 0) return false;
    data += n;
    len -= n;
  }
  return true;
}

// The archive holds the Wi-Fi password (and the web password, if one is set), and in rescue mode
// the hotspot is open, so nobody on it may download it.
static void handleBackup() {
  if (net::isAp()) {
    sendError(403, F("Backups are not available in rescue mode (the hotspot is open). Connect the device to your Wi-Fi first."));
    return;
  }
  if (!settings::fsMounted()) {
    sendError(500, F("Storage is not available"));
    return;
  }
  if (ESP.getMaxFreeBlockSize() < 4096) {
    sendError(503, F("Not enough memory right now, try again in a moment"));
    return;
  }
  // Known beforehand, so the browser can show a progress bar. The display and the web server
  // wait while the file goes out (a few seconds for a couple of megabytes).
  server.setContentLength(backup::tarSize());
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.sendHeader(F("Content-Disposition"), F("attachment; filename=\"status-esp-backup.tar\""));
  server.send(200, F("application/x-tar"), emptyString);   // the headers; the body follows
  WiFiClient &client = server.client();
  backup::writeTar(backupSink, &client);
  client.stop();
}

static bool rsFailed = false;
static int rsCode = 200;
static String rsMessage;

static void rsFail(int code, const char *message) {
  if (rsFailed) return;
  rsFailed = true;
  rsCode = code;
  rsMessage = message;
}

static void handleRestoreChunk() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    rsFailed = false;
    rsCode = 200;
    rsMessage = "";
    if (!authorized()) return rsFail(401, "Password required");
    if (!settings::fsMounted()) return rsFail(500, "Storage is not available");
    media::gifClose();   // the decoder holds a file open, and its memory is needed
    if (!backup::restoreBegin()) return rsFail(503, "Not enough memory right now, try again in a moment");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!rsFailed && !backup::restoreFeed(up.buf, up.currentSize)) rsFail(400, "");   // the reason is in the result
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    rsFail(400, "Upload interrupted");
  }
}

static void handleRestoreDone() {
  if (rsFailed && rsCode == 401) {   // refused at the start: nothing was touched
    rsFailed = false;
    backup::restoreFinish();
    guard();
    return;
  }
  backup::Result r = backup::restoreFinish();   // always: frees the state and removes the temporary file
  bool failed = rsFailed || !r.ok;
  JsonDocument doc;
  doc["ok"] = !failed;
  doc["restored"] = r.restored;
  doc["skipped"] = r.skipped;
  doc["wifi"] = r.haveWifi;
  if (failed) doc["error"] = rsMessage.length() ? rsMessage : String(r.error[0] ? r.error : "The restore failed");
  bool reboot = r.restored > 0 || r.haveWifi;   // the settings in RAM are older than the files now
  doc["reboot"] = reboot;
  rsFailed = false;
  sendJson(failed && !reboot ? (rsCode == 200 ? 400 : rsCode) : 200, doc);
  if (!reboot) return;
  delay(300);
  if (r.haveWifi) net::saveCredentialsAndReboot(r.ssid, r.pass);
  ESP.restart();
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

// Asks for a weather fetch at the next loop pass (the "Check now" button of the Weather tab).
static void handleWeatherRefresh() {
  weather::requestRefresh();
  sendOk();
}

// Asks Status-Portal again at the next loop pass (the Status-Portal tab's "Test connection").
static void handlePortalRefresh() {
  portal::requestRefresh();
  sendOk();
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
  // A playing GIF leaves little heap, and a request needs some (rule 10 in CLAUDE.md): when less than
  // WEB_MIN_HEAP is free the GIF yields. The hook runs on every request, /update included, right after
  // the request line is read and before anything is allocated for it. It never refuses a request.
  server.addHook([](const String &, const String &, WiFiClient *, ESP8266WebServer::ContentTypeFunction) {
    requestHeap = ESP.getFreeHeap();
    requestGif = media::gifIsOpen() ? 1 : 0;
    if (requestGif && requestHeap < config::WEB_MIN_HEAP) {
      media::gifClose();
      requestGif = 2;
    }
    return ESP8266WebServer::CLIENT_REQUEST_CAN_CONTINUE;
  });

  // Must stay before updater.setup(): see handleUpdatePage().
  route("/update", HTTP_GET, handleUpdatePage);
  updater.setup(&server, "/update");

  route("/", HTTP_GET, handleIndex);

  route("/api/status", HTTP_GET, handleStatus);
  route("/api/settings", HTTP_GET, handleSettingsGet);
  route("/api/settings", HTTP_POST, handleSettingsPost);
  route("/api/settings/export", HTTP_GET, handleSettingsExport);
  route("/api/settings/import", HTTP_POST, handleSettingsImport);
  route("/api/wifi/scan", HTTP_GET, handleWifiScan);
  route("/api/wifi", HTTP_POST, handleWifiSave);
  route("/api/files", HTTP_GET, handleFiles);
  server.on("/api/upload", HTTP_POST, handleUploadDone, handleUploadChunk);   // checks the password itself
  route("/api/delete", HTTP_POST, handleDelete);
  route("/api/backup", HTTP_GET, handleBackup);
  server.on("/api/backup/restore", HTTP_POST, handleRestoreDone, handleRestoreChunk);   // checks the password itself
  route("/api/geocode", HTTP_GET, handleGeocode);
  route("/api/weather/refresh", HTTP_POST, handleWeatherRefresh);
  route("/api/portal/refresh", HTTP_POST, handlePortalRefresh);
  route("/api/reboot", HTTP_POST, handleReboot);
  route("/api/factory-reset", HTTP_POST, handleFactoryReset);

  route("/set", HTTP_ANY, handleLegacySet);
  route("/wifi", HTTP_GET, handleLegacyWifiPage);
  route("/wifi", HTTP_POST, handleLegacyWifiSave);
  route("/reboot", HTTP_ANY, handleLegacyReboot);
  server.on("/v.json", handleVersion);   // always open: the upload tools read it
  server.onNotFound(handleNotFound);
}

void loop() {
  if (!listening) {
    // The same moment the 0.1.0 firmware chose: once the Wi-Fi mode is settled.
    if (!net::isConnected() && !net::isAp()) return;
    applyUpdaterCredentials();   // the Wi-Fi mode is settled: password on, or rescue mode and off
    server.begin();
    listening = true;
  }
  server.handleClient();
}

}  // namespace web
