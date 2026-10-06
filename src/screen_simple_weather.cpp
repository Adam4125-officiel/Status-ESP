// Theme "simple_weather" (Simple Weather Clock): a big HH:MM and, below it, the weather icon
// and the current temperature - nothing else.
//
//   y   6..81    HH:MM in Font 8 (75 px digits), hour and minute colours, optional colon blink
//   y 108..204   left: the weather icon (96 px), right: the temperature (Font 6) and its unit
//
// With no weather to show (no city, no network, still loading) the lower half carries the
// same notice the other weather screens use, and the clock keeps running above it.
#include "bigfont.h"
#include "config.h"
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "timekeeping.h"
#include "units.h"
#include "weather.h"
#include "weather_notice.h"

namespace {

const int16_t TIME_Y = 6;
const int16_t AREA_Y = 92;                       // everything below is the weather area
const int16_t AREA_H = config::SCREEN_H - AREA_Y;
const int16_t ICON_CX = 62, ICON_CY = 156, ICON_SIZE = 92;
const int16_t TEMP_X = 124, TEMP_Y = 132;
const int16_t X_HOUR = (config::SCREEN_W - bigfont::HM_W) / 2;
const int16_t X_COLON = X_HOUR + bigfont::PAIR_W;
const int16_t X_MIN = X_COLON + bigfont::COLON_W;

int8_t shownSync;                 // 0 = "--:--", 1 = real time
int8_t shownHour, shownMin, shownColon;
int8_t shownKind;                 // weather_notice::Kind on the screen, -1 = nothing yet
uint32_t shownUpdated;            // weather::Data::updatedAtMs the area was drawn from

void resetShown() {
  shownSync = shownHour = shownMin = shownColon = shownKind = -1;
  shownUpdated = 0;
}

void updateTime() {
  const settings::Settings &s = settings::get();
  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync != 0) {
      bigfont::drawPair(X_HOUR, TIME_Y, "--", TFT_DARKGREY);
      bigfont::drawColon(X_COLON, TIME_Y, true, TFT_DARKGREY);
      bigfont::drawPair(X_MIN, TIME_Y, "--", TFT_DARKGREY);
      shownSync = 0;
      shownHour = shownMin = shownColon = -1;
    }
    return;
  }
  shownSync = 1;

  int hour = t.tm_hour;
  if (s.hour12) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char buf[8];
  if (hour != shownHour) {
    snprintf(buf, sizeof(buf), s.hour12 ? "%d" : "%02d", hour);
    bigfont::drawPair(X_HOUR, TIME_Y, buf, settings::color565(s.hourRgb));
    shownHour = (int8_t)hour;
  }
  if (t.tm_min != shownMin) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    bigfont::drawPair(X_MIN, TIME_Y, buf, settings::color565(s.minRgb));
    shownMin = (int8_t)t.tm_min;
  }
  int8_t colon = (!s.colonBlink || (t.tm_sec % 2) == 0) ? 1 : 0;
  if (colon != shownColon) {
    bigfont::drawColon(X_COLON, TIME_Y, colon, settings::color565(s.hourRgb));
    shownColon = colon;
  }
}

void drawWeather(const weather::Data &w) {
  tft.fillRect(0, AREA_Y, config::SCREEN_W, AREA_H, TFT_BLACK);
  icons::drawWeather(ICON_CX, ICON_CY, ICON_SIZE, w.code, w.isDay);

  char buf[8];
  units::formatWhole(buf, sizeof(buf), units::temperature(w.tempC));
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  int16_t width = tft.textWidth(buf, 6);
  tft.drawString(buf, TEMP_X, TEMP_Y, 6);
  int16_t ringX = TEMP_X + width + 4 + 5;
  display::drawDegree(ringX, TEMP_Y + 8, 5, TFT_WHITE);
  tft.drawString(units::temperatureLabel(), ringX + 5 + 3, TEMP_Y, 4);
}

void updateWeather() {
  weather_notice::Kind kind = weather_notice::current();
  if (kind != weather_notice::NONE) {
    if (shownKind != (int8_t)kind) {
      tft.fillRect(0, AREA_Y, config::SCREEN_W, AREA_H, TFT_BLACK);
      weather_notice::draw(kind);
      shownKind = (int8_t)kind;
      shownUpdated = 0;
    }
    return;
  }
  const weather::Data &w = weather::data();
  if (shownKind != (int8_t)weather_notice::NONE || w.updatedAtMs != shownUpdated) {
    drawWeather(w);
    shownKind = (int8_t)weather_notice::NONE;
    shownUpdated = w.updatedAtMs;
  }
}

}  // namespace

void screenSimpleWeatherEnter() { resetShown(); }

void screenSimpleWeatherUpdate(bool full) {
  if (full) resetShown();
  updateTime();
  updateWeather();
}

void screenSimpleWeatherLeave() {}
