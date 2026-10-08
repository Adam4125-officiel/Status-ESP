// A recording stand-in for TFT_eSPI, so the screens can run on a PC (tools/test_host.sh).
//
// It draws nothing: it counts what a screen asks for, and it flags what a real display would get wrong:
// a string whose box leaves the 240x240 screen, a string wider than the padding that is supposed to
// cover whatever it replaces (the old text would show through), and a character the built-in fonts do
// not have. Only Fonts 2 and 4 are modelled (the two the Status-Portal screens use); the glyph widths are
// the real ones, so text fits here exactly when it fits on the device.
//
// The plastic over a SmallTV-Ultra hides the edge of the glass: on the owner's, half of a 16 px label at y 222
// was not visible. A test that cares sets the safe area, and any string painted outside it is flagged:
// `safeBottom` is the lowest y a string may reach, and a string whose top is at or below `safeSideBelow` must
// stay `safeSide` px from the left and right edges. The defaults flag nothing.
//
// Set `log` and every call is also written as one JSON line (test_portal_screens.cpp does that when
// PORTAL_OPS_DIR is set), which a script can turn into a picture.
#pragma once

#include <Arduino.h>

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_YELLOW 0xFFE0
#define TFT_ORANGE 0xFDA0
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_LIGHTGREY 0xD69A
#define TFT_DARKGREY 0x7BEF

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2

// Glyph advance of the characters 32..127 in Font 2 (Font16.c) and Font 4 (Font32rle.c).
static const unsigned char TFT_WIDTHS_2[96] = {
    6, 3, 4, 9, 8, 9, 9, 3, 7, 7, 8, 6, 3, 6, 5, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 3, 3, 6, 6, 6, 8,
    9, 8, 8, 8, 8, 8, 8, 8, 8, 4, 8, 8, 7, 10, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 10, 8, 8, 8, 4, 7, 4, 7, 9,
    4, 7, 7, 7, 7, 7, 6, 7, 7, 4, 5, 6, 4, 8, 7, 8,
    7, 8, 6, 6, 5, 7, 8, 8, 6, 7, 7, 5, 3, 5, 8, 6,
};
static const unsigned char TFT_WIDTHS_4[96] = {
    5, 8, 8, 19, 14, 21, 17, 6, 8, 8, 12, 10, 7, 8, 7, 8,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 7, 7, 14, 9, 14, 13,
    25, 16, 17, 18, 18, 16, 15, 19, 18, 6, 13, 17, 13, 21, 18, 19,
    16, 19, 17, 16, 14, 18, 15, 23, 15, 16, 16, 9, 13, 9, 12, 13,
    9, 14, 15, 13, 15, 14, 8, 15, 15, 6, 6, 12, 6, 22, 15, 15,
    15, 15, 8, 12, 7, 14, 12, 18, 13, 13, 12, 13, 13, 13, 15, 1,
};

class TFT_eSPI {
 public:
  // What the screens did since resetStats().
  long ops = 0;                       // every drawing call
  long fills = 0;                     // fillScreen() calls
  int violations = 0;                 // see the header
  std::vector<std::string> drawn;     // every string drawn, in order
  std::vector<std::string> problems;  // what each violation was
  FILE *log = nullptr;                // one JSON line per call, when set
  int safeBottom = 240, safeSide = 0, safeSideBelow = 240;   // see the header; survive resetStats()

  void resetStats() {
    ops = fills = 0;
    violations = 0;
    drawn.clear();
    problems.clear();
  }

  void init() {}
  void setRotation(uint8_t) {}
  void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
  void setTextDatum(uint8_t d) { datum_ = d; }
  void setTextPadding(uint16_t p) { pad_ = p; }

  int16_t textWidth(const char *s, uint8_t font) {
    const unsigned char *t = widths(font);
    int w = 0;
    for (; *s; s++) {
      unsigned char c = (unsigned char)*s;
      if (c < 32 || c > 127) {
        problem("character %d is not printable ASCII", c);
        c = 32;
      }
      w += t[c - 32];
    }
    return (int16_t)w;
  }

  int16_t drawString(const char *s, int32_t x, int32_t y, uint8_t font) {
    ops++;
    drawn.push_back(s);
    const int cw = textWidth(s, font), h = font == 4 ? 26 : 16;
    const int span = pad_ > cw ? pad_ : cw;   // what gets painted: the padding box, or the text if wider
    int x0 = (int)x;
    if (datum_ == TC_DATUM) x0 = (int)x - span / 2;
    else if (datum_ == TR_DATUM) x0 = (int)x - span;
    const int tx = datum_ == TC_DATUM ? (int)x - cw / 2 : (datum_ == TR_DATUM ? (int)x - cw : (int)x);   // where the glyphs start
    if (x0 < 0 || x0 + span > 240 || y < 0 || y + h > 240) problem("\"%s\" paints x %d..%d y %d..%d, off the screen", s, x0, x0 + span, (int)y, (int)y + h);
    if (y + h > safeBottom) problem("\"%s\" reaches y %d, below the safe area (%d)", s, (int)y + h, safeBottom);
    if (y >= safeSideBelow && (tx < safeSide || tx + cw > 240 - safeSide)) problem("\"%s\" has glyphs at x %d..%d, closer than %d px to a side", s, tx, tx + cw, safeSide);
    if (pad_ > 0 && cw > pad_) problem("\"%s\" is %d px wide but its padding is %d: the text it replaces would show through", s, cw, pad_);
    if (log) fprintf(log, "{\"op\":\"text\",\"s\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"sx\":%d,\"sw\":%d,\"fg\":%u,\"bg\":%u}\n", s, tx, (int)y, cw, h, x0, span, fg_, bg_);
    return (int16_t)cw;
  }

  void fillScreen(uint16_t c) {
    fills++;
    fillRect(0, 0, 240, 240, c);
  }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) {
    ops++;
    if (w < 0 || h < 0 || x < 0 || y < 0 || x + w > 240 || y + h > 240) problem("fillRect %d,%d %dx%d is off the screen", (int)x, (int)y, (int)w, (int)h);
    if (log) fprintf(log, "{\"op\":\"rect\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"c\":%u}\n", (int)x, (int)y, (int)w, (int)h, c);
  }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t c) {
    ops++;
    if (x - r < 0 || y - r < 0 || x + r >= 240 || y + r >= 240) problem("fillCircle %d,%d r%d is off the screen", (int)x, (int)y, (int)r);
    if (log) fprintf(log, "{\"op\":\"fcircle\",\"x\":%d,\"y\":%d,\"r\":%d,\"c\":%u}\n", (int)x, (int)y, (int)r, c);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t c) {
    ops++;
    if (log) fprintf(log, "{\"op\":\"circle\",\"x\":%d,\"y\":%d,\"r\":%d,\"c\":%u}\n", (int)x, (int)y, (int)r, c);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) {
    ops++;
    if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 >= 240 || x1 >= 240 || y0 >= 240 || y1 >= 240)
      problem("drawLine %d,%d to %d,%d is off the screen", (int)x0, (int)y0, (int)x1, (int)y1);
    if (log) fprintf(log, "{\"op\":\"line\",\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d,\"c\":%u}\n", (int)x0, (int)y0, (int)x1, (int)y1, c);
  }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t c) {
    ops++;
    if (w < 0 || x < 0 || y < 0 || x + w > 240 || y >= 240) problem("drawFastHLine %d,%d w%d is off the screen", (int)x, (int)y, (int)w);
    if (log) fprintf(log, "{\"op\":\"line\",\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d,\"c\":%u}\n", (int)x, (int)y, (int)(x + w - 1), (int)y, c);
  }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint16_t c) {
    ops++;
    if (h < 0 || x < 0 || y < 0 || y + h > 240 || x >= 240) problem("drawFastVLine %d,%d h%d is off the screen", (int)x, (int)y, (int)h);
    if (log) fprintf(log, "{\"op\":\"line\",\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d,\"c\":%u}\n", (int)x, (int)y, (int)x, (int)(y + h - 1), c);
  }

 private:
  uint16_t fg_ = 0xFFFF, bg_ = 0;
  uint8_t datum_ = TL_DATUM;
  int pad_ = 0;

  const unsigned char *widths(uint8_t font) {
    if (font == 2) return TFT_WIDTHS_2;
    if (font == 4) return TFT_WIDTHS_4;
    problem("font %d is not modelled by this stand-in", font);
    return TFT_WIDTHS_2;
  }
  void problem(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    violations++;
    problems.push_back(line);
  }
};

extern TFT_eSPI tft;
