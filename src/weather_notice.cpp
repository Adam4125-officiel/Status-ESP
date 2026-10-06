#include "weather_notice.h"

#include <ESP8266WiFi.h>

#include "display.h"
#include "settings.h"
#include "weather.h"

namespace weather_notice {

namespace {
// millis() when this module first saw "a city, but no data". 0 = not waiting.
uint32_t waitingSince = 0;
}  // namespace

Kind current() {
  if (!settings::hasCity()) {
    waitingSince = 0;
    return NO_CITY;
  }
  if (weather::data().valid) {
    waitingSince = 0;
    return NONE;
  }
  if (WiFi.status() != WL_CONNECTED) return NO_NETWORK;

  uint32_t now = millis();
  if (waitingSince == 0) waitingSince = now ? now : 1;
  return (now - waitingSince >= UNAVAILABLE_AFTER_MS) ? UNAVAILABLE : LOADING;
}

void draw(Kind kind) {
  switch (kind) {
    case NO_CITY:
      display::drawMessage("No city set", "Set a city in the web UI");
      break;
    case NO_NETWORK:
      display::drawMessage("No network", "Weather needs Wi-Fi", TFT_YELLOW);
      break;
    case LOADING:
      display::drawMessage("Loading weather", "Please wait...");
      break;
    case UNAVAILABLE:
      display::drawMessage("No weather data", "Retrying every minute", TFT_YELLOW);
      break;
    default:
      break;
  }
}

}  // namespace weather_notice
