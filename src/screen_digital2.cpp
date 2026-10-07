// Theme "digital2" (Time Style 3): the hour stacked over the minutes in big digits (TFT_eSPI
// Font 8, 75 px), a bar that fills as the seconds go by, and the date.
//
//   y   4..79    HH   (hour colour)
//   y  82..157   MM   (minute colour)
//   y 168..178   seconds bar: 200 px, one 1/60th more every second, empty again at :00
//   y 192..218   weekday and date
//
// Update(false) repaints one thing at a time: a changed digit pair, the next slice of the
// bar (only the new slice is drawn, the bar is never redrawn from scratch except at :00),
// the date line when the day changes. 12/24 h and the colours follow the Time settings.
#include "bigfont.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t HOUR_Y = 4, MIN_Y = 82;
const int16_t DIGITS_X = (config::SCREEN_W - bigfont::PAIR_W) / 2;
const int16_t BAR_X = 20, BAR_Y = 168, BAR_W = 200, BAR_H = 10;
const int16_t DATE_Y = 192;
const uint16_t TRACK_COLOR = 0x2124;   // very dark grey

int8_t shownSync;               // 0 = "--", 1 = real time
int8_t shownHour, shownMin, shownAmPm;
int8_t shownBar;                // seconds drawn in the bar (0..60), -1 = unknown
int32_t shownDay;
int8_t shownHint;

void resetShown() {
  shownSync = shownHour = shownMin = shownAmPm = shownBar = shownHint = -1;
  shownDay = -1;
}

void drawLine(const char *text, int16_t y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(236);
  tft.drawString(text, 120, y, font);
  tft.setTextPadding(0);
}

void drawTrack() { tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, TRACK_COLOR); }

}  // namespace

void screenDigital2Enter() { resetShown(); }

void screenDigital2Update(bool full) {
  const settings::Settings &s = settings::get();
  if (full) resetShown();

  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync != 0) {
      tft.fillRect(0, 0, config::SCREEN_W, config::SCREEN_H, TFT_BLACK);
      resetShown();
      shownSync = 0;
      bigfont::drawPair(DIGITS_X, HOUR_Y, "--", TFT_DARKGREY);
      bigfont::drawPair(DIGITS_X, MIN_Y, "--", TFT_DARKGREY);
    }
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) {
      drawLine(hint == 2 ? "No network - time not available" : "Syncing the time...", DATE_Y, 2, TFT_DARKGREY);
      shownHint = hint;
    }
    return;
  }
  if (shownSync != 1) {  // first real time: start from a clean slate
    tft.fillRect(0, 0, config::SCREEN_W, config::SCREEN_H, TFT_BLACK);
    resetShown();
    shownSync = 1;
  }

  int hour = t.tm_hour;
  bool pm = hour >= 12;
  if (s.hour12) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char buf[32];

  if (hour != shownHour) {
    snprintf(buf, sizeof(buf), s.hour12 ? "%d" : "%02d", hour);
    bigfont::drawPair(DIGITS_X, HOUR_Y, buf, settings::color565(s.hourRgb));
    shownHour = (int8_t)hour;
  }
  if (t.tm_min != shownMin) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    bigfont::drawPair(DIGITS_X, MIN_Y, buf, settings::color565(s.minRgb));
    shownMin = (int8_t)t.tm_min;
  }

  // Bar: second 0 shows one slice, second 59 shows the full bar (never an empty bar while
  // the clock runs); only the slices that are new are drawn.
  int8_t bar = (int8_t)(t.tm_sec + 1);
  if (bar != shownBar) {
    uint16_t fill = settings::color565(s.secRgb);
    if (shownBar < 0 || bar < shownBar) {
      drawTrack();
      shownBar = 0;
    }
    int16_t x0 = BAR_X + (int16_t)((int32_t)BAR_W * shownBar / 60);
    int16_t x1 = BAR_X + (int16_t)((int32_t)BAR_W * bar / 60);
    if (x1 > x0) tft.fillRect(x0, BAR_Y, x1 - x0, BAR_H, fill);
    shownBar = bar;
  }

  int8_t ampm = s.hour12 ? (pm ? 1 : 0) : 2;
  if (ampm != shownAmPm) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextPadding(40);
    tft.drawString(ampm == 2 ? "" : (ampm ? "PM" : "AM"), 236, 8, 4);
    tft.setTextPadding(0);
    shownAmPm = ampm;
  }

  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay) {
    snprintf(buf, sizeof(buf), "%s ", timekeeping::weekdayName(t.tm_wday, false));
    timekeeping::formatDate(buf + strlen(buf), sizeof(buf) - strlen(buf), t);
    drawLine(buf, DATE_Y, 4, TFT_LIGHTGREY);
    shownDay = day;
    shownHint = -1;
  }
}

void screenDigital2Leave() {}
