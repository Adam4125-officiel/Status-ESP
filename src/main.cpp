// Status-ESP: alternative firmware for the GeekMagic SmallTV-Ultra (ESP8266).
//
// Golden rule: /update must ALWAYS stay reachable, on the home Wi-Fi as well
// as in the rescue access point. It is the only way to update (and to go back
// to the stock firmware) without a serial cable.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>

// --- Identity -------------------------------------------------------------
// FW_VERSION comes from the VERSION file, injected at build time by
// tools/version.py (it is a quoted string, e.g. "0.3.0-rc.1").
#ifndef FW_VERSION
#error "FW_VERSION must be defined by tools/version.py (extra_scripts in platformio.ini)"
#endif
#define FW_NAME "Status-ESP"
#define FW_FULL_NAME FW_NAME "-" FW_VERSION   // e.g. "Status-ESP-0.3.0-rc.1"

// --- Hardware -------------------------------------------------------------
// The SmallTV-Ultra has no button and no touch area.
static const uint8_t PIN_BACKLIGHT = 5;

// --- Constants ------------------------------------------------------------
static const char *AP_SSID = FW_NAME;                // rescue access point, open
static const uint32_t WIFI_TIMEOUT_MS = 20000;
// In access-point mode with nobody connected, reboot to retry the Wi-Fi
// (e.g. after a power cut: the router boots more slowly than the display).
static const uint32_t AP_RETRY_MS = 5UL * 60 * 1000;
static const uint32_t STOCK_FW_SIZE = 505200;        // stock firmware 9.0.50 and 9.0.51
// Settings file. The name is a leftover from firmware 0.1.0 and is kept on
// purpose: the owner's device already holds a /custom.json written by 0.1.0,
// and renaming it would silently drop the saved brightness / polarity.
// It does not overwrite any stock file.
static const char *SETTINGS_FILE = "/custom.json";

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

bool fsMounted = false;
bool apMode = false;
uint32_t apStartedAt = 0;
uint8_t brightness = 70;   // 0..100 %
bool blInverted = true;    // backlight polarity, can be changed from the web UI

// --- Persistent settings --------------------------------------------------

void saveSettings() {
  if (!fsMounted) return;
  File f = LittleFS.open(SETTINGS_FILE, "w");
  if (!f) return;
  f.printf("{\"brt\":%u,\"blinv\":%u}", brightness, blInverted ? 1 : 0);
  f.close();
}

void loadSettings() {
  if (!fsMounted || !LittleFS.exists(SETTINGS_FILE)) return;
  File f = LittleFS.open(SETTINGS_FILE, "r");
  String s = f.readString();
  f.close();
  int i = s.indexOf("\"brt\":");
  if (i >= 0) brightness = constrain(s.substring(i + 6).toInt(), 0, 100);
  i = s.indexOf("\"blinv\":");
  if (i >= 0) blInverted = s.substring(i + 8).toInt() != 0;
}

// Reads the Wi-Fi credentials of the stock firmware: {"a":"ssid","p":"password"}
bool readStockWifi(String &ssid, String &pass) {
  if (!fsMounted || !LittleFS.exists("/config.json")) return false;
  File f = LittleFS.open("/config.json", "r");
  String s = f.readString();
  f.close();
  auto field = [&](const char *key) -> String {
    String k = String("\"") + key + "\"";
    int i = s.indexOf(k);
    if (i < 0) return "";
    i = s.indexOf('"', s.indexOf(':', i + k.length()) + 1);
    int j = s.indexOf('"', i + 1);
    return (i < 0 || j < 0) ? "" : s.substring(i + 1, j);
  };
  ssid = field("a");
  pass = field("p");
  return ssid.length() > 0;
}

// --- Display --------------------------------------------------------------

void applyBacklight() {
  uint32_t duty = map(brightness, 0, 100, 0, 1023);
  analogWrite(PIN_BACKLIGHT, blInverted ? 1023 - duty : duty);
}

// Large font if the text fits the width, small font otherwise.
void drawFit(const String &text, int y, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(text, 120, y, tft.textWidth(text, 4) <= 232 ? 4 : 2);
}

void drawStatus(const String &line1, const String &line2, const String &line3 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(FW_NAME, 120, 12, 4);
  tft.drawString("v" FW_VERSION, 120, 42, 2);

  // Colour test pattern (validated on the device: Red / Green / Blue in order).
  tft.fillRect(0, 70, 80, 50, TFT_RED);
  tft.fillRect(80, 70, 80, 50, TFT_GREEN);
  tft.fillRect(160, 70, 80, 50, TFT_BLUE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("R", 40, 87, 2);
  tft.drawString("G", 120, 87, 2);
  tft.drawString("B", 200, 87, 2);

  drawFit(line1, 128, TFT_YELLOW);
  drawFit(line2, 156, TFT_WHITE);
  drawFit(line3, 184, TFT_WHITE);
}

// --- Wi-Fi ----------------------------------------------------------------

bool waitForWifi() {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("status-esp");

  // 1) credentials remembered by the SDK (usually the stock firmware's)
  WiFi.begin();
  if (waitForWifi()) return;

  // 2) credentials from the stock /config.json file
  String ssid, pass;
  if (readStockWifi(ssid, pass)) {
    WiFi.begin(ssid.c_str(), pass.c_str());
    if (waitForWifi()) return;
  }

  // 3) rescue access point: /update and /wifi stay available there
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  apStartedAt = millis();
}

// --- Web pages ------------------------------------------------------------

String htmlPage() {
  uint32_t freeOta = ESP.getFreeSketchSpace();
  bool rollbackOk = freeOta >= STOCK_FW_SIZE;
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  String h;
  h.reserve(2600);
  h += F("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>" FW_NAME "</title><style>"
         "body{font-family:sans-serif;max-width:560px;margin:auto;padding:16px;background:#111;color:#eee}"
         "a,button{color:#fff;background:#2a6;border:0;padding:10px 14px;border-radius:6px;text-decoration:none;display:inline-block;margin:4px 0}"
         ".warn{background:#a62}td{padding:4px 8px}.ok{color:#6d6}.bad{color:#f66}"
         "</style></head><body><h2>" FW_NAME "</h2><table>");
  h += "<tr><td>Version</td><td>" FW_FULL_NAME "</td></tr>";
  h += "<tr><td>IP</td><td>" + ip + (apMode ? " (access point)" : "") + "</td></tr>";
  h += "<tr><td>Free heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
  h += "<tr><td>Firmware size</td><td>" + String(ESP.getSketchSize()) + " bytes</td></tr>";
  h += "<tr><td>Space for /update</td><td>" + String(freeOta) + " bytes</td></tr>";
  h += String("<tr><td>Back to stock firmware</td><td class='") + (rollbackOk ? "ok'>possible" : "bad'>TOO BIG") + "</td></tr>";
  h += String("<tr><td>Stock files</td><td>") + (fsMounted ? "intact (LittleFS mounted)" : "not mounted") + "</td></tr></table>";

  h += "<h3>Brightness: " + String(brightness) + " %</h3>";
  h += F("<input type='range' min='0' max='100' id='b' value='");
  h += String(brightness);
  h += F("' onchange=\"location='/set?brt='+this.value\" style='width:100%'>"
         "<p><a href='/set?blinv=toggle'>Invert backlight</a> "
         "(if the screen stays black or the setting works backwards)</p>"
         "<h3>Wi-Fi</h3><p><a href='/wifi'>Change Wi-Fi network</a></p>"
         "<h3>Update</h3><p><a class='warn' href='/update'>Upload firmware (.bin / .bin.gz)</a></p>"
         "<p>To go back to the stock firmware, upload the official GeekMagic firmware (.bin) on the same page.</p>"
         "<p><a href='/reboot'>Reboot</a></p></body></html>");
  return h;
}

void setupWeb() {
  // /update page without the "FileSystem" form (which would erase the whole
  // stock file area). Registered BEFORE updater.setup() so that it replaces
  // the library's GET page; POST handling stays the library's proven one.
  server.on("/update", HTTP_GET, [] {
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
  });
  updater.setup(&server, "/update");

  server.on("/", [] { server.send(200, "text/html", htmlPage()); });

  server.on("/set", [] {
    if (server.hasArg("brt")) {
      brightness = constrain(server.arg("brt").toInt(), 0, 100);
    }
    if (server.hasArg("blinv")) {
      blInverted = !blInverted;
    }
    applyBacklight();
    saveSettings();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/wifi", HTTP_GET, [] {
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
  });

  server.on("/wifi", HTTP_POST, [] {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.isEmpty()) {
      server.send(400, "text/plain", "Missing SSID");
      return;
    }
    server.send(200, "text/html",
                "<meta charset='utf-8'>Saved. Rebooting... If the connection fails, "
                "the " FW_NAME " network will reappear.");
    delay(300);
    // Store in the SDK's Wi-Fi area, then reboot
    WiFi.persistent(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    delay(500);
    ESP.restart();
  });

  server.on("/reboot", [] {
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
  });

  // Minimal compatibility with the stock interface
  server.on("/v.json", [] {
    server.send(200, "application/json", "{\"m\":\"SmallTV-Ultra\",\"v\":\"" FW_FULL_NAME "\"}");
  });

  server.onNotFound([] {
    if (apMode) {  // simple captive portal: everything redirects to the home page
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302);
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });

  server.begin();
}

// --- Main program ---------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Mount LittleFS WITHOUT ever formatting it: the stock files must survive.
  LittleFSConfig cfg;
  cfg.setAutoFormat(false);
  LittleFS.setConfig(cfg);
  fsMounted = LittleFS.begin();
  loadSettings();

  analogWriteRange(1023);
  analogWriteFreq(1000);
  applyBacklight();

  tft.init();
  tft.setRotation(0);
  drawStatus("Connecting...", "Wi-Fi, up to 40 s");

  startWifi();
  setupWeb();

  if (apMode) {
    drawStatus("No Wi-Fi", String("Network: ") + AP_SSID, "http://192.168.4.1");
  } else {
    drawStatus("Connected", "http://" + WiFi.localIP().toString());
  }
}

void loop() {
  server.handleClient();

  if (apMode && WiFi.softAPgetStationNum() == 0 && millis() - apStartedAt > AP_RETRY_MS) {
    ESP.restart();
  }
}
