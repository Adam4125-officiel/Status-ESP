#include "net.h"

#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "settings.h"

namespace net {

static State st = DELAY;
static uint32_t stateSince = 0;
static uint32_t apStartedAt = 0;

// Reads the Wi-Fi credentials of the stock firmware: {"a":"ssid","p":"password"}
// (kept as the 0.1.0 substring parser: it is the one validated on the real file).
static bool readStockWifi(String &ssid, String &pass) {
  if (!settings::fsMounted() || !LittleFS.exists(config::STOCK_WIFI_FILE)) return false;
  File f = LittleFS.open(config::STOCK_WIFI_FILE, "r");
  if (!f) return false;
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

static void enter(State s) {
  st = s;
  stateSince = millis();
}

static void startSdkAttempt() {
  // The SDK's persistent mode stays on (as in 0.1.0), except across the boot delay,
  // where switching the radio off and on again must not rewrite the flash.
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.hostname(config::HOSTNAME);
  WiFi.begin();  // credentials remembered by the SDK (usually the stock firmware's)
  enter(TRY_SDK);
}

static void startRescueAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config::AP_SSID);
  apStartedAt = millis();
  enter(ACCESS_POINT);
}

void begin() {
  if (settings::get().bootDelay > 0) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    enter(DELAY);
  } else {
    startSdkAttempt();
  }
}

void loop() {
  uint32_t now = millis();
  switch (st) {
    case DELAY:
      if (now - stateSince >= (uint32_t)settings::get().bootDelay * 1000UL) startSdkAttempt();
      break;
    case TRY_SDK:
      if (WiFi.status() == WL_CONNECTED) {
        enter(CONNECTED);
      } else if (now - stateSince >= config::WIFI_TIMEOUT_MS) {
        String ssid, pass;
        if (readStockWifi(ssid, pass)) {
          WiFi.begin(ssid.c_str(), pass.c_str());  // credentials from the stock /config.json
          enter(TRY_STOCK);
        } else {
          startRescueAp();
        }
      }
      break;
    case TRY_STOCK:
      if (WiFi.status() == WL_CONNECTED) {
        enter(CONNECTED);
      } else if (now - stateSince >= config::WIFI_TIMEOUT_MS) {
        startRescueAp();
      }
      break;
    case CONNECTED:
      break;  // the SDK reconnects by itself if the router goes away
    case ACCESS_POINT:
      if (WiFi.softAPgetStationNum() == 0 && now - apStartedAt > config::AP_RETRY_MS) ESP.restart();
      break;
  }
}

State state() { return st; }

uint32_t delayRemaining() {
  if (st != DELAY) return 0;
  uint32_t total = (uint32_t)settings::get().bootDelay * 1000UL;
  uint32_t elapsed = millis() - stateSince;
  return elapsed >= total ? 0 : (total - elapsed + 999) / 1000;
}

String ip() {
  if (st == CONNECTED) return WiFi.localIP().toString();
  if (st == ACCESS_POINT) return WiFi.softAPIP().toString();
  return "";
}

bool scanStart() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return false;
  WiFi.scanDelete();
  // In access-point mode this briefly switches the station interface on too.
  WiFi.scanNetworks(true);
  return true;
}

int scanStatus() { return WiFi.scanComplete(); }

void scanDone() { WiFi.scanDelete(); }

void saveCredentialsAndReboot(const String &ssid, const String &pass) {
  delay(300);
  // Store in the SDK's Wi-Fi area, then reboot
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  delay(500);
  ESP.restart();
}

}  // namespace net
