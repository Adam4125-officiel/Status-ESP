// Theme "binary": a binary-coded-decimal clock. Six columns of dots, two per unit of time
// (hours, minutes, seconds in the three configured colours), each column showing one decimal
// digit in binary with the top dot worth 8 and the bottom one worth 1. The tens columns only
// need the dots that can ever be lit (hours: 2 and 1, minutes and seconds: 4, 2 and 1). The
// time and the date in plain digits sit below, small, so the dots can be checked.
//
//   y  23..153   the dots: radius 11, 32 px between columns, a wider gap between the pairs
//   y 170..196   HH:MM:SS (Font 4, with AM/PM in the 12-hour format)
//   y 204..220   weekday and date
//
// A dot is a filled circle in its colour (on) or a dim one (off); a changed dot is painted
// over in place, so Update(false) touches only the dots that flipped (most seconds: three or
// four of the rightmost column) and nothing flickers.
#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t RADIUS = 11;
const int16_t PITCH = 32;          // between two columns
const int16_t PAIR_GAP = 12;       // extra space between the hour, minute and second pairs
const int16_t FIRST_X = 28;
const int16_t TOP_Y = 34;
const int16_t ROW_PITCH = 36;
const int16_t TIME_Y = 170;
const int16_t DATE_Y = 204;
const uint16_t OFF_COLOR = 0x2124;   // very dark grey

// Dots in each column, counted from the bottom: HH tens, HH units, MM tens, MM units, ...
const uint8_t COLUMN_BITS[6] = {2, 4, 3, 4, 3, 4};

const uint8_t NOTHING = 255;        // shownDigit: nothing drawn yet
const uint8_t ALL_OFF = 254;        // shownDigit / digit: every dot of the column drawn dim

uint8_t shownDigit[6];       // 0..9, ALL_OFF or NOTHING
int32_t shownDay;
int8_t shownSecond;          // second of the plain-text time
int8_t shownHint;

void resetShown() {
  memset(shownDigit, NOTHING, sizeof(shownDigit));
  shownDay = -1;
  shownSecond = -1;
  shownHint = -1;
}

int16_t columnX(uint8_t column) { return FIRST_X + column * PITCH + (column / 2) * PAIR_GAP; }

uint16_t columnColor(uint8_t column) {
  const settings::Settings &s = settings::get();
  uint32_t rgb = column < 2 ? s.hourRgb : (column < 4 ? s.minRgb : s.secRgb);
  return settings::color565(rgb);
}

// Draws the dots of `column` that differ between `from` (NOTHING = nothing drawn yet) and `to`
// (ALL_OFF = every dot dim).
void drawColumn(uint8_t column, uint8_t from, uint8_t to) {
  uint8_t bits = COLUMN_BITS[column];
  uint16_t on = columnColor(column);
  for (uint8_t b = 0; b < bits; b++) {
    bool wasOn = from < 10 && ((from >> b) & 1);
    bool isOn = to < 10 && ((to >> b) & 1);
    if (from != NOTHING && wasOn == isOn) continue;
    int16_t y = TOP_Y + (int16_t)(3 - b) * ROW_PITCH;      // bit 0 is the bottom row
    tft.fillCircle(columnX(column), y, RADIUS, isOn ? on : OFF_COLOR);
  }
}

void setDigit(uint8_t column, uint8_t digit) {
  if (shownDigit[column] == digit) return;
  drawColumn(column, shownDigit[column], digit);
  shownDigit[column] = digit;
}

void drawLine(const char *text, int16_t y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(236);
  tft.drawString(text, 120, y, font);
  tft.setTextPadding(0);
}

}  // namespace

void screenBinaryEnter() { resetShown(); }

void screenBinaryUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) resetShown();

  struct tm t;
  if (!timekeeping::localTime(t)) {
    for (uint8_t c = 0; c < 6; c++) setDigit(c, ALL_OFF);
    if (shownSecond != -2) {
      drawLine("--:--:--", TIME_Y, 4, TFT_DARKGREY);
      shownSecond = -2;
      shownDay = -1;
      shownHint = -1;
    }
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) {
      drawLine(hint == 2 ? "No network - time not available" : "Syncing the time...", DATE_Y, 2, TFT_DARKGREY);
      shownHint = hint;
    }
    return;
  }

  int hour = t.tm_hour;
  bool pm = hour >= 12;
  if (s.hour12) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  setDigit(0, (uint8_t)(hour / 10));
  setDigit(1, (uint8_t)(hour % 10));
  setDigit(2, (uint8_t)(t.tm_min / 10));
  setDigit(3, (uint8_t)(t.tm_min % 10));
  setDigit(4, (uint8_t)(t.tm_sec / 10));
  setDigit(5, (uint8_t)(t.tm_sec % 10));

  char buf[32];
  if (t.tm_sec != shownSecond) {
    snprintf(buf, sizeof(buf), s.hour12 ? "%d:%02d:%02d %s" : "%02d:%02d:%02d%s", hour, t.tm_min, t.tm_sec,
             s.hour12 ? (pm ? "PM" : "AM") : "");
    drawLine(buf, TIME_Y, 4, TFT_WHITE);
    shownSecond = (int8_t)t.tm_sec;
  }
  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay || shownHint != 0) {
    snprintf(buf, sizeof(buf), "%s ", timekeeping::weekdayName(t.tm_wday, false));
    timekeeping::formatDate(buf + strlen(buf), sizeof(buf) - strlen(buf), t);
    drawLine(buf, DATE_Y, 2, TFT_LIGHTGREY);
    shownDay = day;
    shownHint = 0;
  }
}

void screenBinaryLeave() {}
