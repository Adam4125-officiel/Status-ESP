// Theme "analog" (Time Style 2): an analog clock face with hour, minute and second hands,
// and the weekday and date below it.
//
//   y   8..208   the face: 12 hour ticks, 48 minute dots, centre (120, 108), radius 98
//   y 210..236   weekday and date
//
// The hands are filled kites drawn with fillTriangle (the display has no readable frame
// buffer, so nothing anti-aliased). A hand is moved by drawing its previous shape in black
// and the new one in colour, which rasterises the same pixels both times, so only the
// pixels that differ ever change and there is no full-screen clear. The second hand is
// shorter than the ticks start, so erasing it never touches the face; the hour and minute
// hands and the hub are drawn again after it, which repairs whatever it crossed. Hand
// angles are kept as integers (second: 6 degree steps, minute and hour: half degrees) so
// "did it move" is an integer compare and the black and coloured shapes use the same value.
#include <math.h>

#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t CX = 120, CY = 108;
const int16_t FACE_R = 98;
const int16_t DATE_Y = 210;
const float HOUR_LEN = 48, MINUTE_LEN = 72, SECOND_LEN = 78, SECOND_TAIL = 16;

const uint16_t TICK_COLOR = 0xC618;   // light grey
const uint16_t DOT_COLOR = 0x7BEF;    // dark grey

int16_t shownSync;        // 0 = face only, 1 = hands on
int16_t shownHour, shownMin, shownSec;   // half degrees / half degrees / seconds; -1 = none
int32_t shownDay;
int8_t shownHint;

// Point at `length` from the centre in the direction of `degrees` (0 = 12 o'clock, clockwise).
void polar(float degrees, float length, int16_t &x, int16_t &y) {
  float rad = degrees * (float)(M_PI / 180.0);
  x = (int16_t)lroundf(CX + length * sinf(rad));
  y = (int16_t)lroundf(CY - length * cosf(rad));
}

// A kite: tip at `length`, widest (+-halfWidth) at 15 % of the length, tail behind the centre.
void drawKite(float degrees, float length, float halfWidth, float tail, uint16_t color) {
  float rad = degrees * (float)(M_PI / 180.0);
  float dx = sinf(rad), dy = -cosf(rad);          // direction
  float px = -dy, py = dx;                        // perpendicular
  float bx = CX + dx * length * 0.15f, by = CY + dy * length * 0.15f;
  int16_t tipX = (int16_t)lroundf(CX + dx * length), tipY = (int16_t)lroundf(CY + dy * length);
  int16_t lx = (int16_t)lroundf(bx + px * halfWidth), ly = (int16_t)lroundf(by + py * halfWidth);
  int16_t rx = (int16_t)lroundf(bx - px * halfWidth), ry = (int16_t)lroundf(by - py * halfWidth);
  int16_t tx = (int16_t)lroundf(CX - dx * tail), ty = (int16_t)lroundf(CY - dy * tail);
  tft.fillTriangle(tipX, tipY, lx, ly, rx, ry, color);
  tft.fillTriangle(lx, ly, rx, ry, tx, ty, color);
}

void drawHour(int16_t halfDeg, uint16_t color) { drawKite(halfDeg * 0.5f, HOUR_LEN, 4.5f, 10, color); }
void drawMinute(int16_t halfDeg, uint16_t color) { drawKite(halfDeg * 0.5f, MINUTE_LEN, 3.0f, 10, color); }

void drawSecond(int16_t sec, uint16_t color) {
  int16_t x1, y1, x2, y2;
  polar(sec * 6.0f, SECOND_LEN, x1, y1);
  polar(sec * 6.0f + 180.0f, SECOND_TAIL, x2, y2);
  tft.drawLine(x2, y2, x1, y1, color);
  tft.drawLine(x2 + 1, y2, x1 + 1, y1, color);   // two pixels wide
  tft.drawLine(x2, y2 + 1, x1, y1 + 1, color);
}

void drawHub(const settings::Settings &s) {
  tft.fillCircle(CX, CY, 6, settings::color565(s.hourRgb));
  tft.fillCircle(CX, CY, 3, settings::color565(s.secRgb));
}

void drawFace() {
  for (int i = 0; i < 60; i++) {
    int16_t x1, y1, x2, y2;
    if (i % 5 == 0) {
      bool quarter = i % 15 == 0;
      polar(i * 6.0f, FACE_R, x1, y1);
      polar(i * 6.0f, FACE_R - (quarter ? 14 : 11), x2, y2);
      float rad = i * 6.0f * (float)(M_PI / 180.0);
      int16_t ox = (int16_t)lroundf(cosf(rad)), oy = (int16_t)lroundf(sinf(rad));   // perpendicular step
      tft.drawLine(x1, y1, x2, y2, TICK_COLOR);
      tft.drawLine(x1 + ox, y1 + oy, x2 + ox, y2 + oy, TICK_COLOR);
      if (quarter) tft.drawLine(x1 - ox, y1 - oy, x2 - ox, y2 - oy, TICK_COLOR);
    } else {
      polar(i * 6.0f, FACE_R - 2, x1, y1);
      tft.fillRect(x1 - 1, y1 - 1, 2, 2, DOT_COLOR);
    }
  }
}

void drawLine(const char *text, uint16_t color, uint8_t font) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(236);
  tft.drawString(text, 120, DATE_Y, font);
  tft.setTextPadding(0);
}

void drawHint(int8_t kind) {
  drawLine(kind == 2 ? "No network" : "Syncing the time...", TFT_DARKGREY, 2);
  shownHint = kind;
}

void resetShown() {
  shownSync = -1;
  shownHour = shownMin = shownSec = -1;
  shownDay = -1;
  shownHint = -1;
}

}  // namespace

void screenAnalogEnter() { resetShown(); }

void screenAnalogUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) {
    resetShown();
    drawFace();
  }

  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync == 1) {  // the clock became unavailable again: take the hands off
      if (shownSec >= 0) drawSecond(shownSec, TFT_BLACK);
      if (shownMin >= 0) drawMinute(shownMin, TFT_BLACK);
      if (shownHour >= 0) drawHour(shownHour, TFT_BLACK);
      tft.fillCircle(CX, CY, 6, TFT_BLACK);
      shownHour = shownMin = shownSec = -1;
    }
    shownSync = 0;
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) {
      drawHint(hint);
      shownDay = -1;
    }
    return;
  }
  shownSync = 1;

  int16_t hourIdx = (int16_t)((t.tm_hour % 12) * 60 + t.tm_min);               // half degrees
  int16_t minIdx = (int16_t)((t.tm_min * 60 + t.tm_sec) / 5);                  // half degrees
  int16_t secIdx = (int16_t)t.tm_sec;

  if (hourIdx != shownHour || minIdx != shownMin || secIdx != shownSec) {
    if (shownSec >= 0) drawSecond(shownSec, TFT_BLACK);
    if (shownMin >= 0 && shownMin != minIdx) drawMinute(shownMin, TFT_BLACK);
    if (shownHour >= 0 && shownHour != hourIdx) drawHour(shownHour, TFT_BLACK);
    drawHour(hourIdx, settings::color565(s.hourRgb));
    drawMinute(minIdx, settings::color565(s.minRgb));
    drawSecond(secIdx, settings::color565(s.secRgb));
    drawHub(s);
    shownHour = hourIdx;
    shownMin = minIdx;
    shownSec = secIdx;
  }

  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s ", timekeeping::weekdayName(t.tm_wday, false));
    timekeeping::formatDate(buf + strlen(buf), sizeof(buf) - strlen(buf), t);
    drawLine(buf, TFT_LIGHTGREY, 4);
    shownDay = day;
    shownHint = -1;
  }
}

void screenAnalogLeave() {}
