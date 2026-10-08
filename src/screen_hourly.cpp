// Theme "hourly": the next 24 hours of the weather on one screen.
//
//   y   3..28   the chosen city, and the temperature now
//   y  32..116  the next six hours side by side: hour, icon, temperature, chance of rain
//   y 120       divider
//   y 124..140  the label of the highest temperature of the 24 hours
//   y 142..176  the 24 hours as a curve of the temperature (a dot at the highest and the lowest)
//   y 178..194  the label of the lowest
//   y 198..218  the chance of rain of each hour, as bars
//   y 222..238  the hour of every sixth point
//
// Only Fonts 2 and 4, like the Status-Portal screens: the PC tests (tests/host) know exactly those two,
// and a layout that fits there fits on the device. Nothing here changes between two weather updates
// except the hour the day has reached, so Update(false) compares what the screen was drawn from and
// otherwise does nothing (no drawing call at all).
#include "config.h"
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "timekeeping.h"
#include "units.h"
#include "weather.h"
#include "weather_notice.h"

namespace {

const int16_t COLS = 6, COL_W = 40;
const int16_t CITY_Y = 8, NOW_Y = 2;
const int16_t HOUR_Y = 32, ICON_CY = 64, ICON_SIZE = 28, TEMP_Y = 82, RAIN_Y = 100;
const int16_t DIVIDER_Y = 120;
const int16_t CH_X0 = 14, CH_X1 = 226;                 // the 24 points are spread over this
const int16_t MAX_LABEL_Y = 124, CURVE_TOP = 142, CURVE_BOTTOM = 176, MIN_LABEL_Y = 178;
const int16_t BAR_TOP = 198, BAR_BOTTOM = 218, TICK_Y = 222;
const int16_t MIN_RANGE_DC = 30;                       // a flat day is not drawn as a mountain range

const uint16_t HIGH_COLOR = TFT_ORANGE;
const uint16_t LOW_COLOR = 0x5D5F;                     // light blue (RGB565 of 90,170,255)
const uint16_t RAIN_COLOR = 0x5D5F;
const uint16_t DIVIDER_COLOR = 0x2124;                 // very dark grey

int8_t shownKind;        // weather_notice::Kind on the screen, -1 = nothing yet
uint32_t shownUpdated;   // weather::Data::updatedAtMs the screen was drawn from
int16_t shownSkip;       // how many of the hours had already gone by
uint8_t shownKey;        // the unit and the hour format it was drawn in
bool shownNoHours;       // the "no hourly data" message is what is on screen

void clearScreen() { tft.fillRect(0, 0, config::SCREEN_W, config::SCREEN_H, TFT_BLACK); }

// `text` centred on cx at y, kept inside the screen whatever cx is.
void drawCentered(const char *text, int16_t cx, int16_t y, uint8_t font, uint16_t color) {
  int16_t w = tft.textWidth(text, font);
  int16_t half = w / 2;
  if (cx < half) cx = half;
  if (cx > config::SCREEN_W - (w - half)) cx = config::SCREEN_W - (w - half);
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(text, cx - half, y, font);
}

// "<number><ring>" centred on cx, top-aligned at y.
void drawTemperature(int16_t cx, int16_t y, float celsius, uint8_t font, uint16_t color) {
  char buf[8];
  units::formatWhole(buf, sizeof(buf), units::temperature(celsius));
  const int16_t ringR = font == 4 ? 3 : 2;
  int16_t w = tft.textWidth(buf, font);
  int16_t total = w + 2 + 2 * ringR + 1;
  int16_t x = cx - total / 2;
  if (x < 0) x = 0;
  if (x + total > config::SCREEN_W) x = config::SCREEN_W - total;
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(buf, x, y, font);
  display::drawDegree(x + w + 2 + ringR, y + (font == 4 ? 7 : 4), ringR, color);
}

void hourLabel(char *buf, size_t size, int hour) {
  if (settings::get().hour12) {
    int h = hour % 12;
    snprintf(buf, size, "%d%s", h == 0 ? 12 : h, hour < 12 ? "am" : "pm");
  } else {
    snprintf(buf, size, "%02dh", hour);
  }
}

// The city name at the left in Font 2, shortened with "..." until it fits beside the temperature now.
void drawCity() {
  char name[sizeof(settings::get().city)];
  strlcpy(name, settings::get().city, sizeof(name));
  const int16_t maxW = 128;
  size_t len = strlen(name);
  if (tft.textWidth(name, 2) > maxW) {
    while (len > 3 && tft.textWidth(name, 2) + tft.textWidth("...", 2) > maxW) name[--len] = '\0';
    strlcat(name, "...", sizeof(name));
  }
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(name, 6, CITY_Y, 2);
}

// How many of the answer's hours have already gone by. The first one is the hour the answer was made in; the
// hours after it are told from the portal-independent clock when it is set (the city's own hour, from the
// offset the same answer carried), and from how long ago the answer arrived when it is not.
int16_t hoursElapsed(const weather::Data &w) {
  if (w.hourN == 0 || w.hourFirst >= 24) return 0;
  if (timekeeping::synced()) {
    int64_t cityNow = (int64_t)timekeeping::utcNow() + w.utcOffsetSeconds;
    int hourNow = (int)((cityNow / 3600) % 24);
    if (hourNow < 0) hourNow += 24;
    return (int16_t)((hourNow - (int)w.hourFirst + 24) % 24);
  }
  uint32_t ageHours = (millis() - w.updatedAtMs) / 3600000UL;
  return ageHours > 23 ? 23 : (int16_t)ageHours;
}

void drawStrip(const weather::Data &w, int16_t skip, int16_t avail) {
  for (int16_t i = 0; i < COLS && i < avail; i++) {
    const weather::Hour &h = w.hours[skip + i];
    int16_t cx = i * COL_W + COL_W / 2;
    char buf[8];
    hourLabel(buf, sizeof(buf), (w.hourFirst + skip + i) % 24);
    drawCentered(buf, cx, HOUR_Y, 2, TFT_LIGHTGREY);
    icons::drawWeather(cx, ICON_CY, ICON_SIZE, h.code, h.isDay != 0);
    drawTemperature(cx, TEMP_Y, h.tempDc / 10.0f, 2, TFT_WHITE);
    if (h.rain == weather::RAIN_UNKNOWN) {
      drawCentered("-", cx, RAIN_Y, 2, TFT_DARKGREY);
    } else {
      snprintf(buf, sizeof(buf), "%u%%", (unsigned)h.rain);
      drawCentered(buf, cx, RAIN_Y, 2, h.rain >= 30 ? RAIN_COLOR : TFT_DARKGREY);
    }
  }
}

int16_t chartX(int16_t i) { return CH_X0 + (int16_t)((int32_t)(CH_X1 - CH_X0) * i / (weather::HOURLY_MAX - 1)); }

void drawChart(const weather::Data &w, int16_t skip, int16_t avail) {
  int16_t n = avail > weather::HOURLY_MAX ? weather::HOURLY_MAX : avail;
  int16_t lo = w.hours[skip].tempDc, hi = lo, iLo = 0, iHi = 0;
  for (int16_t i = 1; i < n; i++) {
    int16_t t = w.hours[skip + i].tempDc;
    if (t < lo) { lo = t; iLo = i; }
    if (t > hi) { hi = t; iHi = i; }
  }
  int16_t floorDc = lo, range = hi - lo;
  if (range < MIN_RANGE_DC) {   // centre a flat curve instead of laying it on the floor
    floorDc = lo - (MIN_RANGE_DC - range) / 2;
    range = MIN_RANGE_DC;
  }
  const int16_t height = CURVE_BOTTOM - CURVE_TOP;
  auto yOf = [&](int16_t dc) { return (int16_t)(CURVE_BOTTOM - (int32_t)(dc - floorDc) * height / range); };

  for (int16_t i = 0; i + 1 < n; i++) {
    int16_t x0 = chartX(i), x1 = chartX(i + 1);
    int16_t y0 = yOf(w.hours[skip + i].tempDc), y1 = yOf(w.hours[skip + i + 1].tempDc);
    tft.drawLine(x0, y0, x1, y1, HIGH_COLOR);
    tft.drawLine(x0, y0 + 1, x1, y1 + 1, HIGH_COLOR);   // two pixels thick
  }
  tft.fillCircle(chartX(iHi), yOf(hi), 3, HIGH_COLOR);
  drawTemperature(chartX(iHi), MAX_LABEL_Y, hi / 10.0f, 2, HIGH_COLOR);
  if (hi != lo) {
    tft.fillCircle(chartX(iLo), yOf(lo), 3, LOW_COLOR);
    drawTemperature(chartX(iLo), MIN_LABEL_Y, lo / 10.0f, 2, LOW_COLOR);
  }

  // The chance of rain of each hour, from a baseline.
  tft.drawFastHLine(CH_X0 - 4, BAR_BOTTOM, CH_X1 - CH_X0 + 8, DIVIDER_COLOR);
  for (int16_t i = 0; i < n; i++) {
    uint8_t rain = w.hours[skip + i].rain;
    if (rain == weather::RAIN_UNKNOWN || rain == 0) continue;
    int16_t h = (int16_t)((int32_t)rain * (BAR_BOTTOM - BAR_TOP) / 100);
    if (h < 1) h = 1;
    tft.fillRect(chartX(i) - 2, BAR_BOTTOM - h, 5, h, RAIN_COLOR);
  }

  for (int16_t i = 0; i < n; i += 6) {
    char buf[8];
    hourLabel(buf, sizeof(buf), (w.hourFirst + skip + i) % 24);
    drawCentered(buf, chartX(i), TICK_Y, 2, TFT_DARKGREY);
  }
}

void drawHourly(const weather::Data &w, int16_t skip, int16_t avail) {
  clearScreen();
  drawCity();
  drawTemperature(config::SCREEN_W - 40, NOW_Y, w.tempC, 4, TFT_WHITE);
  drawStrip(w, skip, avail);
  tft.drawFastHLine(6, DIVIDER_Y, config::SCREEN_W - 12, DIVIDER_COLOR);
  drawChart(w, skip, avail);
}

uint8_t currentKey() { return (uint8_t)(settings::get().tempUnit | (settings::get().hour12 ? 2 : 0)); }

}  // namespace

void screenHourlyEnter() {
  shownKind = -1;
  shownUpdated = 0;
  shownSkip = -1;
  shownKey = 255;
  shownNoHours = false;
}

void screenHourlyUpdate(bool full) {
  if (full) screenHourlyEnter();   // the manager cleared the screen: draw everything again

  weather_notice::Kind kind = weather_notice::current();
  if (kind != weather_notice::NONE) {
    if (shownKind != (int8_t)kind) {
      clearScreen();
      weather_notice::draw(kind);
      shownKind = (int8_t)kind;
      shownUpdated = 0;
      shownNoHours = false;
    }
    return;
  }

  const weather::Data &w = weather::data();
  int16_t skip = hoursElapsed(w);
  int16_t avail = (int16_t)w.hourN - skip;
  bool noHours = avail < 1;
  uint8_t key = currentKey();
  if (shownKind == (int8_t)weather_notice::NONE && w.updatedAtMs == shownUpdated && skip == shownSkip &&
      key == shownKey && noHours == shownNoHours)
    return;

  if (noHours) {
    clearScreen();
    display::drawMessage("No hourly forecast", "Waiting for the next weather update", TFT_YELLOW);
  } else {
    drawHourly(w, skip, avail);
  }
  shownKind = (int8_t)weather_notice::NONE;
  shownUpdated = w.updatedAtMs;
  shownSkip = skip;
  shownKey = key;
  shownNoHours = noHours;
}

void screenHourlyLeave() {}
