// Theme "weather_clock": the time and date on top, today's conditions below, and the
// user's 80x80 GIF (setting weather_gif, a file in /gif) where the big icon would be.
//
//   y   4..52   HH:MM in the clock font (colours, 12/24 h, colon blink), seconds and AM/PM
//   y  56..82   date
//   y  88..168  left: the GIF, or the weather icon when there is none (or it cannot play)
//               right: temperature, conditions, "feels like"
//   y 186..230  humidity | wind | pressure, in the user's units
//
// Update(false) repaints one thing at a time: a changed time field, the colons, the date
// when the day changes, the whole lower area when weather::data() changed, one GIF frame.
// Every text has an opaque background and a fixed-width field, so nothing is erased first.
//
// The GIF decoder (~24 KB in one block) and the weather fetch never coexist: the fetch
// closes the decoder, and this screen notices the GIF is gone and reopens it. A GIF that
// cannot open (no memory, missing or bad file) falls back to the icon and is retried every
// GIF_RETRY_MS, never on every pass.
#include "config.h"
#include "display.h"
#include "icons.h"
#include "media.h"
#include "settings.h"
#include "timekeeping.h"
#include "units.h"
#include "weather.h"
#include "weather_notice.h"

namespace {

const int16_t TIME_Y = 4;
const int16_t TIME_H = 48;
const int16_t SEC_H = 26;             // Font 4
const int16_t SIDE_GAP = 6;           // between the minutes and the seconds / AM-PM column
const int16_t DATE_Y = 56;
const int16_t AREA_Y = 86;            // the weather area is everything below this line
const int16_t AREA_H = config::SCREEN_H - AREA_Y;
const int16_t BOX_X = 6, BOX_Y = 88, BOX_SIZE = 80;
const int16_t COL_X = 94;
const int16_t TEMP_Y = 88, DESC_Y = 140, FEELS_Y = 160;
const int16_t CAPTION_Y = 186, VALUE_Y = 204;
const uint32_t GIF_RETRY_MS = 10000;

// --- Time row -----------------------------------------------------------------------

uint8_t fontId = 7;
int16_t digitW = 32, colonW = 12, sideW = 28;
int16_t xHour, xColon, xMin, xSide;

// What is on the screen right now (-1 = unknown, forces a repaint).
int8_t shownSync;            // 0 = "--:--", 1 = real time
int8_t shownHour, shownMin, shownSec, shownColon, shownAmPm;   // AM/PM: 0 AM, 1 PM, 2 nothing
int32_t shownDay;

// --- Weather area ---------------------------------------------------------------------

enum BoxMode : uint8_t { BOX_EMPTY, BOX_ICON, BOX_GIF };

int8_t shownKind;            // weather_notice::Kind on the screen, -1 = nothing yet
uint32_t shownUpdated;       // weather::Data::updatedAtMs the area was drawn from
BoxMode boxMode;
uint32_t gifNextTry;         // millis() before which a failed GIF is not retried

void resetTimeShown() {
  shownSync = shownHour = shownMin = shownSec = shownColon = shownAmPm = -1;
  shownDay = -1;
}

void resetShown() {
  resetTimeShown();
  shownKind = -1;
  shownUpdated = 0;
  boxMode = BOX_EMPTY;
  gifNextTry = millis();
}

void layoutTime() {
  fontId = settings::get().clockFont == settings::FONT_PLAIN ? 6 : 7;
  digitW = tft.textWidth("0", fontId);
  colonW = tft.textWidth(":", fontId);
  int16_t secW = tft.textWidth("00", 4);
  int16_t ampmW = tft.textWidth("PM", 2);
  sideW = secW > ampmW ? secW : ampmW;
  int16_t total = 4 * digitW + colonW + SIDE_GAP + sideW;
  xHour = (config::SCREEN_W - total) / 2;
  xColon = xHour + 2 * digitW;
  xMin = xColon + colonW;
  xSide = xMin + 2 * digitW + SIDE_GAP;
}

void drawField(int16_t x, const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(2 * digitW);
  tft.drawString(text, x + 2 * digitW, TIME_Y, fontId);
  tft.setTextPadding(0);
}

void drawColon(bool visible, uint16_t color) {
  if (visible) {
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(":", xColon, TIME_Y, fontId);
  } else {
    tft.fillRect(xColon, TIME_Y, colonW, TIME_H, TFT_BLACK);
  }
}

void drawSide(const char *text, int16_t y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(sideW);
  tft.drawString(text, xSide, y, font);
  tft.setTextPadding(0);
}

void updateTime() {
  const settings::Settings &s = settings::get();
  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync != 0) {
      tft.fillRect(0, 0, config::SCREEN_W, AREA_Y, TFT_BLACK);
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextDatum(TC_DATUM);
      tft.drawString("--:--", config::SCREEN_W / 2, TIME_Y, fontId);
      resetTimeShown();
      shownSync = 0;
    }
    return;
  }
  if (shownSync != 1) {  // first real time: start the top part from a clean slate
    tft.fillRect(0, 0, config::SCREEN_W, AREA_Y, TFT_BLACK);
    resetTimeShown();
    shownSync = 1;
  }

  int hour = t.tm_hour;
  bool pm = hour >= 12;
  if (s.hour12) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char buf[20];

  if (hour != shownHour) {
    snprintf(buf, sizeof(buf), s.hour12 ? "%d" : "%02d", hour);
    drawField(xHour, buf, settings::color565(s.hourRgb));
    shownHour = (int8_t)hour;
  }
  if (t.tm_min != shownMin) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    drawField(xMin, buf, settings::color565(s.minRgb));
    shownMin = (int8_t)t.tm_min;
  }
  if (t.tm_sec != shownSec) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
    drawSide(buf, TIME_Y + TIME_H - SEC_H, 4, settings::color565(s.secRgb));
    shownSec = (int8_t)t.tm_sec;
  }

  int8_t colon = (!s.colonBlink || (t.tm_sec % 2) == 0) ? 1 : 0;
  if (colon != shownColon) {
    drawColon(colon, settings::color565(s.hourRgb));
    shownColon = colon;
  }

  int8_t ampm = s.hour12 ? (pm ? 1 : 0) : 2;
  if (ampm != shownAmPm) {
    drawSide(ampm == 2 ? "" : (ampm ? "PM" : "AM"), TIME_Y, 2, TFT_LIGHTGREY);
    shownAmPm = ampm;
  }

  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay) {
    char date[32];
    snprintf(date, sizeof(date), "%s ", timekeeping::weekdayName(t.tm_wday, false));
    timekeeping::formatDate(date + strlen(date), sizeof(date) - strlen(date), t);
    display::drawFit(date, DATE_Y, TFT_LIGHTGREY);
    shownDay = day;
  }
}

// --- Weather area -----------------------------------------------------------------------

// "<number><ring><unit>" from x, top-aligned at y. Returns the x after it.
int16_t drawTemperature(int16_t x, int16_t y, float celsius, uint8_t numFont, uint8_t unitFont,
                        int16_t ringRadius, int16_t ringDy, uint16_t color) {
  char buf[8];
  units::formatWhole(buf, sizeof(buf), units::temperature(celsius));
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  int16_t w = tft.textWidth(buf, numFont);
  tft.drawString(buf, x, y, numFont);
  int16_t ringX = x + w + 2 + ringRadius;
  display::drawDegree(ringX, y + ringDy, ringRadius, color);
  int16_t labelX = ringX + ringRadius + 3;
  tft.drawString(units::temperatureLabel(), labelX, y, unitFont);
  return labelX + tft.textWidth(units::temperatureLabel(), unitFont);
}

void formatWind(char *buf, size_t size, float kmh) {
  float v = units::wind(kmh);
  if (settings::get().windUnit == settings::WIND_MS) {
    snprintf(buf, size, "%.1f", v);   // m/s: whole numbers are too coarse
  } else {
    snprintf(buf, size, "%d", (int)lroundf(v));
  }
}

void drawStat(int16_t cx, const char *caption, const char *value) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(caption, cx, CAPTION_Y, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(value, cx, VALUE_Y, 4);
}

void clearBox() { tft.fillRect(BOX_X, BOX_Y, BOX_SIZE, BOX_SIZE, TFT_BLACK); }

void drawIcon(const weather::Data &w) {
  clearBox();
  icons::drawWeather(BOX_X + BOX_SIZE / 2, BOX_Y + BOX_SIZE / 2, BOX_SIZE, w.code, w.isDay);
  boxMode = BOX_ICON;
}

// Repaints the whole lower area from the cached data. The GIF box is left empty: updateGif()
// fills it with the animation, or with the icon when there is none.
void drawWeatherArea(const weather::Data &w) {
  tft.fillRect(0, AREA_Y, config::SCREEN_W, AREA_H, TFT_BLACK);
  boxMode = BOX_EMPTY;

  drawTemperature(COL_X, TEMP_Y, w.tempC, 6, 4, 4, 8, TFT_WHITE);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.drawString(icons::describe(w.code), COL_X, DESC_Y, 2);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("Feels ", COL_X, FEELS_Y, 2);
  drawTemperature(COL_X + tft.textWidth("Feels ", 2), FEELS_Y, w.feelsC, 2, 2, 2, 4, TFT_DARKGREY);

  char caption[24], value[16];
  snprintf(value, sizeof(value), "%u%%", (unsigned)w.humidity);
  drawStat(40, "Humidity", value);
  snprintf(caption, sizeof(caption), "Wind %s", units::windLabel());
  formatWind(value, sizeof(value), w.windKmh);
  drawStat(120, caption, value);
  snprintf(caption, sizeof(caption), "Press %s", units::pressureLabel());
  units::formatPressure(value, sizeof(value), w.pressureHpa);
  drawStat(200, caption, value);
}

void closeGif() {
  media::gifClose();
  boxMode = BOX_EMPTY;
}

// The left box: the user's GIF while it plays, the icon otherwise.
void updateGif(const weather::Data &w) {
  const settings::Settings &s = settings::get();

  if (!s.weatherGif[0]) {  // no GIF chosen (or it was just cleared)
    if (media::gifIsOpen()) media::gifClose();
    if (boxMode != BOX_ICON) drawIcon(w);
    return;
  }

  if (media::gifIsOpen()) {
    if (media::gifPlayFrame()) return;
    media::gifClose();   // it cannot decode: show the icon and try again later
    gifNextTry = millis() + GIF_RETRY_MS;
    drawIcon(w);
    return;
  }

  // Not open. If it was playing, the weather fetch took the decoder: reopen it right away.
  if (boxMode == BOX_GIF) gifNextTry = millis();
  uint32_t now = millis();
  if ((int32_t)(now - gifNextTry) < 0) {
    if (boxMode != BOX_ICON) drawIcon(w);   // waiting for the retry: the icon stands in
    return;
  }

  char path[config::MAX_FILE_NAME + 8];
  snprintf(path, sizeof(path), "%s/%s", config::DIR_GIF, s.weatherGif);
  if (media::gifOpen(path, BOX_X, BOX_Y, true)) {
    clearBox();          // the first frame is drawn over black, not over the icon
    boxMode = BOX_GIF;
    return;
  }
  gifNextTry = now + GIF_RETRY_MS;
  if (boxMode != BOX_ICON) drawIcon(w);
}

void updateWeather() {
  weather_notice::Kind kind = weather_notice::current();
  if (kind != weather_notice::NONE) {
    if (shownKind != (int8_t)kind) {
      closeGif();
      tft.fillRect(0, AREA_Y, config::SCREEN_W, AREA_H, TFT_BLACK);
      weather_notice::draw(kind);
      shownKind = (int8_t)kind;
      shownUpdated = 0;
    }
    return;
  }

  const weather::Data &w = weather::data();
  if (shownKind != (int8_t)weather_notice::NONE || w.updatedAtMs != shownUpdated) {
    closeGif();   // the area is about to be cleared: the animation restarts on the new picture
    drawWeatherArea(w);
    shownKind = (int8_t)weather_notice::NONE;
    shownUpdated = w.updatedAtMs;
    gifNextTry = millis();
  }
  updateGif(w);
}

}  // namespace

void screenWeatherEnter() {
  layoutTime();
  resetShown();
}

void screenWeatherUpdate(bool full) {
  if (full) {
    // The manager cleared the screen: forget what was drawn, and restart the GIF.
    media::gifClose();
    layoutTime();   // a clock-font change arrives as a full repaint
    resetShown();
  }
  updateTime();
  updateWeather();
}

void screenWeatherLeave() { media::gifClose(); }
