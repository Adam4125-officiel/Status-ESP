// Status-ESP: alternative firmware for the GeekMagic SmallTV-Ultra (ESP8266).
//
// Golden rule: /update must ALWAYS stay reachable, on the home Wi-Fi as well
// as in the rescue access point. It is the only way to update (and to go back
// to the stock firmware) without a serial cable. So nothing in loop() may block
// for long: every module is a small state machine called once per pass.
//
// Modules: settings (config + LittleFS), net (Wi-Fi), web (routes and API),
// timekeeping (SNTP, offset, night window), weather (Open-Meteo cache), media
// (JPG / GIF), display (screen manager) and the screen_*.cpp themes.
#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "mdns.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"
#include "weather.h"
#include "web.h"

// How long the "Connected, <ip>" screen stays before the themes start.
static const uint32_t IP_SCREEN_MS = 10000;

// Keeps the boot / information screen in step with the Wi-Fi state machine.
static void updateBootScreen() {
  static int8_t shown = -1;
  static uint32_t shownRemaining = 0xFFFFFFFF;

  net::State state = net::state();
  if (state == net::DELAY) {
    uint32_t remaining = net::delayRemaining();
    if (shown != net::DELAY || remaining != shownRemaining) {
      char line[24];
      snprintf(line, sizeof(line), "%lu s", (unsigned long)remaining);
      display::showStatus("Waiting to connect", line, "Delay set in the web UI");
      shownRemaining = remaining;
      shown = net::DELAY;
    }
    return;
  }

  bool connecting = state == net::TRY_SDK || state == net::TRY_STOCK;
  int8_t group = connecting ? net::TRY_SDK : (int8_t)state;
  if (group == shown) return;
  shown = group;

  switch (state) {
    case net::TRY_SDK:
    case net::TRY_STOCK:
      display::showStatus("Connecting...", "Wi-Fi, up to 40 s");
      break;
    case net::CONNECTED:
      display::showStatus("Connected", ("http://" + net::ip()).c_str(), mdns::name().c_str());
      display::showThemesAfter(IP_SCREEN_MS);
      break;
    case net::ACCESS_POINT:  // the rescue instructions stay on screen
      display::showStatus("No Wi-Fi", (String("Network: ") + config::AP_SSID).c_str(),
                          (String("http://") + config::AP_IP).c_str());
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);

  settings::begin();   // mounts LittleFS WITHOUT formatting, loads /custom.json
  display::begin();    // backlight, TFT
  weather::begin();
  net::begin();        // non-blocking: boot delay, then the connection attempts
  web::begin();        // routes only; listening starts once the Wi-Fi mode is settled

  display::showStatus("Connecting...", "Wi-Fi, up to 40 s");
}

void loop() {
  web::loop();
  net::loop();
  updateBootScreen();
  timekeeping::loop();
  weather::loop();
  display::loop();
}
