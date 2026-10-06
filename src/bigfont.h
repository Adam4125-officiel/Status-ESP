// TFT_eSPI Font 8 (the "8N" build: Arial digits, 75 px high, 53 px wide) helpers shared by
// the themes that show big digits: clock ("Large" font), digital2, simple_weather and
// countdown. Font 8 only contains " 0123456789:-." (anything else prints as a space) and a
// 2-digit field is 106 px, so "HH:MM" would be 241 px with the font's own colon: it is
// drawn by hand instead (a narrower, blinkable colon), which makes the row 236 px.
//
// Everything is opaque (text background black, fixed-width padding) so a changed field is
// repainted in place with no clearing and no flicker.
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

namespace bigfont {

constexpr uint8_t ID = 8;
constexpr int16_t DIGIT_W = 53;
constexpr int16_t HEIGHT = 75;
constexpr int16_t PAIR_W = 2 * DIGIT_W;
constexpr int16_t COLON_W = 24;
constexpr int16_t HM_W = 2 * PAIR_W + COLON_W;   // 236

// Draws up to two digits right-aligned in a 106 px field whose top-left is (x, y).
inline void drawPair(int16_t x, int16_t y, const char *text, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(PAIR_W);
  tft.drawString(text, x + PAIR_W, y, ID);
  tft.setTextPadding(0);
}

// Two dots in a COLON_W x HEIGHT cell at (x, y); `visible == false` blanks the cell.
inline void drawColon(int16_t x, int16_t y, bool visible, uint16_t color) {
  tft.fillRect(x, y, COLON_W, HEIGHT, TFT_BLACK);
  if (!visible) return;
  tft.fillRect(x + 7, y + 19, 10, 10, color);
  tft.fillRect(x + 7, y + 44, 10, 10, color);
}

}  // namespace bigfont
