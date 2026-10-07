// Theme "forecast": the three days after today, side by side.
//
//   y   6..22   the chosen city (trimmed to fit)
//   y  32..58   weekday
//   y  72..128  icon (day version)
//   y 138..164  highest temperature, warm colour
//   y 170..196  lowest temperature, cool colour
//   y 214..230  legend and the temperature unit
//
// Nothing here changes between two weather updates, so Update(false) only checks whether
// weather::data() is new (or the reason for having none changed) and otherwise does nothing.
// A settings change (units, city) arrives as Update(true) through CH_VISUAL.
#include "config.h"
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "timekeeping.h"
#include "units.h"
#include "weather.h"
#include "weather_notice.h"

namespace {

const int16_t COL_W = 80;
const int16_t CITY_Y = 6;
const int16_t WEEKDAY_Y = 32;
const int16_t ICON_CY = 100, ICON_SIZE = 56;
const int16_t HIGH_Y = 138, LOW_Y = 170;
const int16_t FOOT_Y = 214;
const int16_t DIVIDER_TOP = 32, DIVIDER_BOTTOM = 200;

const uint16_t HIGH_COLOR = TFT_ORANGE;
const uint16_t LOW_COLOR = 0x5D5F;     // light blue (RGB565 of 90,170,255)
const uint16_t DIVIDER_COLOR = 0x2124; // very dark grey

int8_t shownKind;        // weather_notice::Kind on the screen, -1 = nothing yet
uint32_t shownUpdated;   // weather::Data::updatedAtMs the screen was drawn from

void clearScreen() { tft.fillRect(0, 0, config::SCREEN_W, config::SCREEN_H, TFT_BLACK); }

// The city name centred in Font 2, shortened with "..." until it fits.
void drawCity() {
  char name[sizeof(settings::get().city)];
  strlcpy(name, settings::get().city, sizeof(name));
  const int16_t maxW = config::SCREEN_W - 8;
  size_t len = strlen(name);
  if (tft.textWidth(name, 2) > maxW) {
    while (len > 3 && tft.textWidth(name, 2) + tft.textWidth("...", 2) > maxW) name[--len] = '\0';
    strlcat(name, "...", sizeof(name));
  }
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(name, config::SCREEN_W / 2, CITY_Y, 2);
}

// "<number><ring>" centred on cx, top-aligned at y, in Font 4.
void drawTemperatureCentered(int16_t cx, int16_t y, float celsius, uint16_t color) {
  char buf[8];
  units::formatWhole(buf, sizeof(buf), units::temperature(celsius));
  int16_t w = tft.textWidth(buf, 4);
  const int16_t ringR = 3;
  int16_t total = w + 2 + 2 * ringR + 1;
  int16_t x = cx - total / 2;
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(buf, x, y, 4);
  display::drawDegree(x + w + 2 + ringR, y + 7, ringR, color);
}

// One piece of the footer, drawn from x. Returns the x after it.
int16_t footPiece(int16_t x, const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(text, x, FOOT_Y, 2);
  return x + tft.textWidth(text, 2);
}

// "High  Low  <ring>C", centred. The ring and the unit tell what the numbers are in.
void drawFooter() {
  const int16_t gap = 12, ringR = 2;
  const char *unit = units::temperatureLabel();
  int16_t total = tft.textWidth("High", 2) + gap + tft.textWidth("Low", 2) + gap + 2 * ringR + 1 + 3 +
                  tft.textWidth(unit, 2);
  int16_t x = (config::SCREEN_W - total) / 2;
  x = footPiece(x, "High", HIGH_COLOR) + gap;
  x = footPiece(x, "Low", LOW_COLOR) + gap;
  display::drawDegree(x + ringR, FOOT_Y + 4, ringR, TFT_DARKGREY);
  footPiece(x + 2 * ringR + 1 + 3, unit, TFT_DARKGREY);
}

void drawForecast(const weather::Data &w) {
  clearScreen();
  drawCity();
  for (int i = 0; i < 3; i++) {
    const weather::Day &d = w.forecast[i];
    int16_t cx = i * COL_W + COL_W / 2;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(0);
    tft.drawString(timekeeping::weekdayName(d.wday, false), cx, WEEKDAY_Y, 4);
    icons::drawWeather(cx, ICON_CY, ICON_SIZE, d.code, true);
    drawTemperatureCentered(cx, HIGH_Y, d.maxC, HIGH_COLOR);
    drawTemperatureCentered(cx, LOW_Y, d.minC, LOW_COLOR);
    if (i > 0) tft.drawFastVLine(i * COL_W, DIVIDER_TOP, DIVIDER_BOTTOM - DIVIDER_TOP, DIVIDER_COLOR);
  }
  drawFooter();
}

}  // namespace

void screenForecastEnter() {
  shownKind = -1;
  shownUpdated = 0;
}

void screenForecastUpdate(bool full) {
  if (full) screenForecastEnter();   // the manager cleared the screen: draw everything again

  weather_notice::Kind kind = weather_notice::current();
  if (kind != weather_notice::NONE) {
    if (shownKind != (int8_t)kind) {
      clearScreen();
      weather_notice::draw(kind);
      shownKind = (int8_t)kind;
      shownUpdated = 0;
    }
    return;
  }

  const weather::Data &w = weather::data();
  if (shownKind != (int8_t)weather_notice::NONE || w.updatedAtMs != shownUpdated) {
    drawForecast(w);
    shownKind = (int8_t)weather_notice::NONE;
    shownUpdated = w.updatedAtMs;
  }
}

void screenForecastLeave() {}
