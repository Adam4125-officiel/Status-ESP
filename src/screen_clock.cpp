// Theme "clock": date, a big HH:MM:SS in three colours, 12/24 h, optional colon blink.
//
// Redraws only what changed: a field is repainted when its value changes (seconds once
// a second), the colons when the blink phase flips, the date lines when the day changes.
// Every text uses an opaque background and a fixed-width field, so nothing is erased
// first and nothing flickers. Font 7 (7-segment, "Digital") and Font 6 ("Plain") are
// both 48 px high; "HH:MM:SS" is 216 / 192 px wide, so one layout serves both.
//
// The "Large" font (Font 8, 75 px digits) cannot fit "HH:MM:SS" on a 240 px screen (it is
// 236 px for HH:MM alone), so that layout is different: the weekday, HH:MM in Font 8, the
// seconds in Font 7 below it with AM/PM beside them, and the date. It is selected by
// `large` and every drawing step below has a branch for it; the shared state is the same.
#include "bigfont.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t Y_WEEKDAY = 34;
const int16_t Y_TIME = 80;
const int16_t TIME_H = 48;
const int16_t Y_AMPM = 134;
const int16_t Y_DATE = 160;
const int16_t Y_HINT = 214;

// Layout of the Large font.
const int16_t L_Y_WEEKDAY = 6;
const int16_t L_Y_TIME = 40;
const int16_t L_Y_SEC = 124;
const int16_t L_Y_AMPM = 140;
const int16_t L_Y_DATE = 184;
const int16_t L_X_HOUR = (config::SCREEN_W - bigfont::HM_W) / 2;
const int16_t L_X_COLON = L_X_HOUR + bigfont::PAIR_W;
const int16_t L_X_MIN = L_X_COLON + bigfont::COLON_W;

bool large = false;

uint8_t fontId = 7;
int16_t digitW = 32, colonW = 12;
int16_t xHour, xColon1, xMin, xColon2, xSec;

// What is on the screen right now (-1 = unknown, forces a repaint).
int8_t shownSync;          // 0 = "--:--:--", 1 = real time
int8_t shownHour, shownMin, shownSec;
int8_t shownColon;         // 0 hidden, 1 shown
int8_t shownAmPm;          // 0 AM, 1 PM, 2 = nothing (24 h)
int32_t shownDay;
int8_t shownHint;          // 0 none, 1 syncing, 2 no network, 3 UTC notice

void resetShown() {
  shownSync = shownHour = shownMin = shownSec = shownColon = shownAmPm = shownHint = -1;
  shownDay = -1;
}

void drawField(int16_t x, const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(2 * digitW);
  tft.drawString(text, x + 2 * digitW, Y_TIME, fontId);
  tft.setTextPadding(0);
}

// Large layout: seconds in Font 7, centred.
void drawLargeSeconds(const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(72);
  tft.drawString(text, 120, L_Y_SEC, 7);
  tft.setTextPadding(0);
}

void drawColon(int16_t x, bool visible, uint16_t color) {
  if (visible) {
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(":", x, Y_TIME, fontId);
  } else {
    tft.fillRect(x, Y_TIME, colonW, TIME_H, TFT_BLACK);
  }
}

void drawLine(const char *text, int16_t y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(236);
  tft.drawString(text, 120, y, font);
  tft.setTextPadding(0);
}

void drawHint(int8_t kind) {
  const char *text = "";
  if (kind == 1) text = "Syncing the time...";
  else if (kind == 2) text = "No network - time not available";
  else if (kind == 3) text = "UTC - set a city or an offset";
  drawLine(text, Y_HINT, 2, TFT_DARKGREY);
  shownHint = kind;
}

void drawUnsynced() {
  tft.fillRect(0, 0, 240, 240, TFT_BLACK);
  if (large) {
    bigfont::drawPair(L_X_HOUR, L_Y_TIME, "--", TFT_DARKGREY);
    bigfont::drawColon(L_X_COLON, L_Y_TIME, true, TFT_DARKGREY);
    bigfont::drawPair(L_X_MIN, L_Y_TIME, "--", TFT_DARKGREY);
  } else {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("--:--:--", 120, Y_TIME, fontId);
  }
  shownSync = 0;
  shownHint = -1;
}

}  // namespace

void screenClockEnter() {
  const settings::Settings &s = settings::get();
  large = s.clockFont == settings::FONT_LARGE;
  fontId = s.clockFont == settings::FONT_PLAIN ? 6 : 7;
  digitW = tft.textWidth("0", fontId);
  colonW = tft.textWidth(":", fontId);
  int16_t total = 6 * digitW + 2 * colonW;
  xHour = (config::SCREEN_W - total) / 2;
  xColon1 = xHour + 2 * digitW;
  xMin = xColon1 + colonW;
  xColon2 = xMin + 2 * digitW;
  xSec = xColon2 + colonW;
  resetShown();
}

void screenClockUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) {
    // A font change arrives as a full repaint: re-measure.
    screenClockEnter();
  }

  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync != 0) drawUnsynced();
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) drawHint(hint);
    return;
  }
  if (shownSync != 1) {  // first real time after "--:--:--": start from a clean slate
    tft.fillRect(0, 0, 240, 240, TFT_BLACK);
    resetShown();
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
    if (large) bigfont::drawPair(L_X_HOUR, L_Y_TIME, buf, settings::color565(s.hourRgb));
    else drawField(xHour, buf, settings::color565(s.hourRgb));
    shownHour = (int8_t)hour;
  }
  if (t.tm_min != shownMin) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    if (large) bigfont::drawPair(L_X_MIN, L_Y_TIME, buf, settings::color565(s.minRgb));
    else drawField(xMin, buf, settings::color565(s.minRgb));
    shownMin = (int8_t)t.tm_min;
  }
  if (t.tm_sec != shownSec) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
    if (large) drawLargeSeconds(buf, settings::color565(s.secRgb));
    else drawField(xSec, buf, settings::color565(s.secRgb));
    shownSec = (int8_t)t.tm_sec;
  }

  int8_t colon = (!s.colonBlink || (t.tm_sec % 2) == 0) ? 1 : 0;
  if (colon != shownColon) {
    if (large) {
      bigfont::drawColon(L_X_COLON, L_Y_TIME, colon, settings::color565(s.hourRgb));
    } else {
      drawColon(xColon1, colon, settings::color565(s.hourRgb));
      drawColon(xColon2, colon, settings::color565(s.minRgb));
    }
    shownColon = colon;
  }

  int8_t ampm = s.hour12 ? (pm ? 1 : 0) : 2;
  if (ampm != shownAmPm) {
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextPadding(40);
    tft.drawString(ampm == 2 ? "" : (ampm ? "PM" : "AM"), large ? 236 : xSec + 2 * digitW, large ? L_Y_AMPM : Y_AMPM, 4);
    tft.setTextPadding(0);
    shownAmPm = ampm;
  }

  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay) {
    drawLine(timekeeping::weekdayName(t.tm_wday, true), large ? L_Y_WEEKDAY : Y_WEEKDAY, 4, TFT_WHITE);
    timekeeping::formatDate(buf, sizeof(buf), t);
    drawLine(buf, large ? L_Y_DATE : Y_DATE, 4, TFT_LIGHTGREY);
    shownDay = day;
  }

  int8_t hint = timekeeping::offsetKnown() ? 0 : 3;
  if (hint != shownHint) drawHint(hint);
}

void screenClockLeave() {}
