// Theme "rings": three concentric rings around the digital time. The outer ring is the
// seconds, the middle one the minutes and the inner one the hours, in the three configured
// colours; each fills clockwise from 12 o'clock over a dim track.
//
//   radius 108..117   seconds   60 steps of 6 degrees, one more every second
//   radius  96..105   minutes   60 steps, one more every minute
//   radius  84.. 93   hours     60 steps: one every 12 minutes (12-hour format) or every
//                               24 minutes (24-hour format), so it fills over the half day
//                               or the whole day; empty never, like the others (step 1 is
//                               the first slice of the period)
//   inside            AM/PM, HH:MM in Font 6, the date and the weekday
//
// A ring is only ever drawn forward: Update(false) paints the new slices and nothing else.
// When a ring wraps (second 59 -> 0) its track is repainted and the first slice drawn, which
// takes a few tens of milliseconds with no full-screen clear. A slice is the set of pixels of
// the ring whose centre lies between two radial lines (see slice()): the 60 slices tile the
// ring exactly, so the colour covers the track pixel for pixel and nothing shows between two
// coloured slices.
#include <math.h>

#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t CX = 120, CY = 120;
const int16_t THICK = 9;
const int16_t SEC_OUT = 117, MIN_OUT = 105, HOUR_OUT = 93;
const uint8_t STEPS = 60;
const uint16_t TRACK = 0x2124;     // very dark grey

const int16_t AMPM_Y = 74;
const int16_t TIME_Y = 96;         // Font 6: 48 px high
const int16_t DATE_Y = 152;
const int16_t WEEKDAY_Y = 170;
const int16_t DATE_PAD = 128;      // 2 * sqrt(84^2 - 48^2) = 138 free px at the bottom of the date line
const int16_t WEEKDAY_PAD = 96;    // 2 * sqrt(84^2 - 66^2) = 104 at the bottom of the weekday line

int16_t digitW = 28, colonW = 12;
int16_t xHour, xColon, xMin;

int8_t shownSec, shownMinK, shownHourK;   // slices drawn in each ring (0..60), -1 = unknown
int8_t shownSync;                         // 0 = "--:--", 1 = real time
int8_t shownHour, shownMin, shownColon, shownAmPm;
int32_t shownDay;
int8_t shownHint;

void resetShown() {
  shownSec = shownMinK = shownHourK = -1;
  shownSync = shownHour = shownMin = shownColon = shownAmPm = shownHint = -1;
  shownDay = -1;
}

// Unit vector of the direction `k` * 6 degrees clockwise from 12 o'clock, in screen coordinates
// (y down), scaled by 4096. The same function gives a slice's end and the next slice's start,
// so two neighbours share their boundary exactly.
void direction(uint8_t k, int32_t &ux, int32_t &uy) {
  float a = k * 6.0f * (float)(M_PI / 180.0);
  ux = (int32_t)lroundf(4096.0f * sinf(a));
  uy = (int32_t)lroundf(-4096.0f * cosf(a));
}

// One slice of a ring: the pixels between `outer - THICK` and `outer` from the centre whose
// direction lies in [i * 6, i * 6 + 6) degrees. Decided pixel by pixel in integers (the pixel's
// centre against the two radial lines and the two circles), so the 60 slices of a ring tile it
// exactly - no gap, no overlap - and a coloured slice covers the same pixels as its track.
// Coordinates are doubled so that pixel centres are integers: X = 2x + 1 - 2 * CX.
void slice(int16_t outer, uint8_t i, uint16_t color) {
  int32_t ux0, uy0, ux1, uy1;
  direction(i, ux0, uy0);
  direction(i + 1, ux1, uy1);
  int32_t rOut = 2 * outer, rIn = 2 * (outer - THICK);
  int32_t out2 = rOut * rOut, in2 = rIn * rIn;

  // Bounding box from the four corners, a pixel wider for the bulge of the arcs.
  int16_t x0 = config::SCREEN_W, x1 = 0, y0 = config::SCREEN_H, y1 = 0;
  for (int corner = 0; corner < 4; corner++) {
    int32_t ux = corner & 1 ? ux1 : ux0, uy = corner & 1 ? uy1 : uy0;
    int32_t r = corner & 2 ? rIn : rOut;
    int16_t x = (int16_t)(CX + ux * r / (2 * 4096)), y = (int16_t)(CY + uy * r / (2 * 4096));
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  x0 = x0 > 1 ? x0 - 1 : 0;
  y0 = y0 > 1 ? y0 - 1 : 0;
  x1 = x1 < config::SCREEN_W - 2 ? x1 + 1 : config::SCREEN_W - 1;
  y1 = y1 < config::SCREEN_H - 2 ? y1 + 1 : config::SCREEN_H - 1;

  for (int16_t y = y0; y <= y1; y++) {
    int32_t Y = 2 * (y - CY) + 1;
    int16_t runStart = -1;
    for (int16_t x = x0; x <= x1 + 1; x++) {
      bool inside = false;
      if (x <= x1) {
        int32_t X = 2 * (x - CX) + 1;
        int32_t rr = X * X + Y * Y;
        inside = rr >= in2 && rr < out2 && ux0 * Y - uy0 * X >= 0 && ux1 * Y - uy1 * X < 0;
      }
      if (inside && runStart < 0) runStart = x;
      if (!inside && runStart >= 0) {
        tft.drawFastHLine(runStart, y, x - runStart, color);
        runStart = -1;
      }
    }
  }
}

// Brings a ring to `target` slices (0..60). Going backwards (a wrap, or the clock being set)
// repaints the track first.
void setRing(int16_t outer, int8_t &shown, int8_t target, uint16_t color) {
  if (shown == target) return;
  if (shown < 0 || target < shown) {
    for (uint8_t i = 0; i < STEPS; i++) slice(outer, i, TRACK);
    shown = 0;
  }
  for (int8_t i = shown; i < target; i++) slice(outer, (uint8_t)i, color);
  shown = target;
}

void drawPair(int16_t xLeft, const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(2 * digitW);
  tft.drawString(text, xLeft + 2 * digitW, TIME_Y, 6);
  tft.setTextPadding(0);
}

void drawColon(bool visible) {
  if (visible) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(":", xColon, TIME_Y, 6);
  } else {
    tft.fillRect(xColon, TIME_Y, colonW, 48, TFT_BLACK);
  }
}

// Centred Font 2 line. `pad` is the width that is blanked around it: it must stay inside the
// hour ring (radius 84) at that height, or the repaint would eat a piece of the ring.
void drawText(const char *text, int16_t y, uint16_t color, int16_t pad) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(pad);
  tft.drawString(text, 120, y, 2);
  tft.setTextPadding(0);
}

void measure() {
  digitW = tft.textWidth("0", 6);
  colonW = tft.textWidth(":", 6);
  int16_t total = 4 * digitW + colonW;
  xHour = (config::SCREEN_W - total) / 2;
  xColon = xHour + 2 * digitW;
  xMin = xColon + colonW;
}

}  // namespace

void screenRingsEnter() {
  measure();
  resetShown();
}

void screenRingsUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) screenRingsEnter();

  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownSync != 0) {
      setRing(SEC_OUT, shownSec, 0, TRACK);
      setRing(MIN_OUT, shownMinK, 0, TRACK);
      setRing(HOUR_OUT, shownHourK, 0, TRACK);
      tft.fillRect(xHour, TIME_Y, 4 * digitW + colonW, 48, TFT_BLACK);
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("--:--", xHour + (4 * digitW + colonW - tft.textWidth("--:--", 6)) / 2, TIME_Y, 6);
      shownSync = 0;
      shownHour = shownMin = shownColon = shownAmPm = -1;
      shownDay = -1;
      shownHint = -1;
    }
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) {
      drawText(hint == 2 ? "No network" : "Syncing...", DATE_Y, TFT_DARKGREY, DATE_PAD);
      drawText("", WEEKDAY_Y, TFT_DARKGREY, WEEKDAY_PAD);
      shownHint = hint;
    }
    return;
  }
  if (shownSync != 1) {   // first real time: take the "--:--" off
    tft.fillRect(xHour, TIME_Y, 4 * digitW + colonW, 48, TFT_BLACK);
    shownSync = 1;
    shownHour = shownMin = shownColon = shownAmPm = -1;
    shownDay = -1;
    shownHint = -1;
  }

  setRing(SEC_OUT, shownSec, (int8_t)(t.tm_sec < 59 ? t.tm_sec + 1 : 60), settings::color565(s.secRgb));
  setRing(MIN_OUT, shownMinK, (int8_t)(t.tm_min + 1), settings::color565(s.minRgb));
  int minutesIntoPeriod = s.hour12 ? (t.tm_hour % 12) * 60 + t.tm_min : t.tm_hour * 60 + t.tm_min;
  setRing(HOUR_OUT, shownHourK, (int8_t)(minutesIntoPeriod / (s.hour12 ? 12 : 24) + 1), settings::color565(s.hourRgb));

  int hour = t.tm_hour;
  bool pm = hour >= 12;
  if (s.hour12) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  char buf[24];
  if (hour != shownHour) {
    snprintf(buf, sizeof(buf), s.hour12 ? "%d" : "%02d", hour);
    drawPair(xHour, buf, settings::color565(s.hourRgb));
    shownHour = (int8_t)hour;
  }
  if (t.tm_min != shownMin) {
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    drawPair(xMin, buf, settings::color565(s.minRgb));
    shownMin = (int8_t)t.tm_min;
  }
  int8_t colon = (!s.colonBlink || (t.tm_sec % 2) == 0) ? 1 : 0;
  if (colon != shownColon) {
    drawColon(colon);
    shownColon = colon;
  }
  int8_t ampm = s.hour12 ? (pm ? 1 : 0) : 2;
  if (ampm != shownAmPm) {
    drawText(ampm == 2 ? "" : (ampm ? "PM" : "AM"), AMPM_Y, TFT_LIGHTGREY, 40);
    shownAmPm = ampm;
  }
  int32_t day = (int32_t)t.tm_year * 400 + t.tm_yday;
  if (day != shownDay) {
    timekeeping::formatDate(buf, sizeof(buf), t);
    drawText(buf, DATE_Y, TFT_LIGHTGREY, DATE_PAD);
    drawText(timekeeping::weekdayName(t.tm_wday, true), WEEKDAY_Y, TFT_WHITE, WEEKDAY_PAD);
    shownDay = day;
    shownHint = -1;
  }
}

void screenRingsLeave() {}
