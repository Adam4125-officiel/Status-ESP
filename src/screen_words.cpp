// Theme "words": an English word clock. The time is spelled out by lighting words in a grid of
// letters ("IT IS A QUARTER TO THREE"), to the nearest five minutes, with four dots below
// that count the minutes past the last five.
//
// The grid is 12 columns by 11 rows of 20 px cells (the letters are TFT_eSPI Font 2). The
// arrangement is our own; the words are the ones English needs:
//
//   ITKISWBDHALF      IT IS                     HALF
//   AXQUARTERTEN      A QUARTER                 TEN
//   TWENTYMFIVEZ      TWENTY                    FIVE
//   PASTKTOQONEL      PAST   TO                 ONE
//   TWOJTENHSIXV      TWO    TEN                SIX
//   THREEQFOURZD      THREE  FOUR
//   FIVEBSEVENKW      FIVE   SEVEN
//   EIGHTYNINEPJ      EIGHT  NINE
//   ZELEVENHQDBU        ELEVEN
//   MBTWELVEFGYX          TWELVE
//   PHQRUZOCLOCK                    OCLOCK
//
// Colours: the hour word is lit in the hour colour, the minutes (FIVE, TEN, A QUARTER, TWENTY,
// HALF) in the minute colour, the little words (IT IS, PAST, TO, OCLOCK) in the second colour,
// and the four dots in the minute colour. Everything else is a dim letter.
//
// Update(false) compares the new lit pattern with the one on the screen and repaints only the
// letters that changed, which at most is a few words once every five minutes (the dots move
// every minute). A letter is drawn with an opaque background in a cell of fixed size, so a
// colour change paints over the old one and nothing flickers.
#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const uint8_t COLS = 12, ROWS = 11;
const int16_t CELL = 20;
const int16_t GRID_Y = 2;
const int16_t DOTS_Y = 231;
const int16_t DOT_R = 3, DOT_PITCH = 20;
const uint16_t DIM = 0x4208;          // an unlit letter or dot: dark grey

const char *const GRID[ROWS] = {
    "ITKISWBDHALF", "AXQUARTERTEN", "TWENTYMFIVEZ", "PASTKTOQONEL", "TWOJTENHSIXV", "THREEQFOURZD",
    "FIVEBSEVENKW", "EIGHTYNINEPJ", "ZELEVENHQDBU", "MBTWELVEFGYX", "PHQRUZOCLOCK",
};

// What a lit cell is lit as (the grid cell's value; 0 = dim).
enum : uint8_t { K_OFF = 0, K_HOUR = 1, K_MINUTE = 2, K_LITTLE = 3 };

struct Word {
  uint8_t row, col, len;
};

const Word W_IT = {0, 0, 2}, W_IS = {0, 3, 2}, W_HALF = {0, 8, 4};
const Word W_A = {1, 0, 1}, W_QUARTER = {1, 2, 7}, W_TEN = {1, 9, 3};
const Word W_TWENTY = {2, 0, 6}, W_FIVE = {2, 7, 4};
const Word W_PAST = {3, 0, 4}, W_TO = {3, 5, 2};
const Word W_OCLOCK = {10, 6, 6};
// Hours 1..12 (index 0 is unused).
const Word HOURS[13] = {{0, 0, 0}, {3, 8, 3}, {4, 0, 3}, {5, 0, 5}, {5, 6, 4}, {6, 0, 4}, {4, 8, 3},
                        {6, 5, 5}, {7, 0, 5}, {7, 6, 4}, {4, 4, 3}, {8, 1, 6}, {9, 2, 6}};

uint8_t lit[ROWS][COLS];       // what the pattern for the current time says
uint8_t shown[ROWS][COLS];     // what is on the screen; 255 = nothing drawn
int8_t shownDots;              // minutes past the five, 0..4 (-1 = nothing drawn)
int8_t shownHint;

void put(const Word &w, uint8_t kind) {
  for (uint8_t i = 0; i < w.len; i++) lit[w.row][w.col + i] = kind;
}

// Fills `lit` for the time: the words for the nearest five minutes at or below `minute`.
void buildPattern(int hour, int minute) {
  memset(lit, K_OFF, sizeof(lit));
  put(W_IT, K_LITTLE);
  put(W_IS, K_LITTLE);
  int five = minute / 5;           // 0..11
  if (five == 0) put(W_OCLOCK, K_LITTLE);
  else if (five == 5 || five == 7) { put(W_TWENTY, K_MINUTE); put(W_FIVE, K_MINUTE); }
  else if (five == 1 || five == 11) put(W_FIVE, K_MINUTE);
  else if (five == 2 || five == 10) put(W_TEN, K_MINUTE);
  else if (five == 3 || five == 9) { put(W_A, K_MINUTE); put(W_QUARTER, K_MINUTE); }
  else if (five == 4 || five == 8) put(W_TWENTY, K_MINUTE);
  else put(W_HALF, K_MINUTE);        // 6
  if (five >= 1 && five <= 6) put(W_PAST, K_LITTLE);
  else if (five >= 7) put(W_TO, K_LITTLE);
  int h = hour % 12;
  if (five >= 7) h++;              // "to": the next hour
  if (h == 0) h = 12;
  put(HOURS[h], K_HOUR);
}

uint16_t cellColor(uint8_t kind, const settings::Settings &s) {
  switch (kind) {
    case K_HOUR: return settings::color565(s.hourRgb);
    case K_MINUTE: return settings::color565(s.minRgb);
    case K_LITTLE: return settings::color565(s.secRgb);
    default: return DIM;
  }
}

void drawCell(uint8_t row, uint8_t col, uint8_t kind, const settings::Settings &s) {
  char ch[2] = {GRID[row][col], '\0'};
  tft.setTextColor(cellColor(kind, s), TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(ch, col * CELL + CELL / 2, GRID_Y + row * CELL + CELL / 2, 2);
}

void drawDots(int8_t count, const settings::Settings &s) {
  uint16_t on = settings::color565(s.minRgb);
  for (int8_t i = 0; i < 4; i++) {
    int16_t x = 120 + (i - 2) * DOT_PITCH + DOT_PITCH / 2;
    tft.fillCircle(x, DOTS_Y, DOT_R, i < count ? on : DIM);
  }
}

// Repaints the cells whose state differs from what is on the screen.
void paintChanges(const settings::Settings &s) {
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      if (shown[r][c] == lit[r][c]) continue;
      drawCell(r, c, lit[r][c], s);
      shown[r][c] = lit[r][c];
    }
  }
}

void resetShown() {
  memset(shown, 255, sizeof(shown));
  shownDots = -1;
  shownHint = -1;
}

void drawHint(const char *text) {
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(236);
  tft.drawString(text, 120, DOTS_Y - 8, 2);
  tft.setTextPadding(0);
}

}  // namespace

void screenWordsEnter() { resetShown(); }

void screenWordsUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) resetShown();

  struct tm t;
  if (!timekeeping::localTime(t)) {
    // Only "IT IS" is lit until the clock is set; the dots give way to a short notice.
    if (shownDots != -2) {
      memset(lit, K_OFF, sizeof(lit));
      put(W_IT, K_LITTLE);
      put(W_IS, K_LITTLE);
      paintChanges(s);
      tft.fillRect(0, DOTS_Y - 8, 240, 16, TFT_BLACK);
      shownDots = -2;
      shownHint = -1;
    }
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) {
      drawHint(hint == 2 ? "No network - time not available" : "Syncing the time...");
      shownHint = hint;
    }
    return;
  }

  buildPattern(t.tm_hour, t.tm_min);
  paintChanges(s);
  if (shownHint != 0 || shownDots == -2) {   // the notice (or nothing) is on the dots' row: clear it
    tft.fillRect(0, DOTS_Y - 8, 240, 16, TFT_BLACK);
    shownHint = 0;
    shownDots = -1;
  }
  int8_t dots = (int8_t)(t.tm_min % 5);
  if (dots != shownDots) {
    drawDots(dots, s);
    shownDots = dots;
  }
}

void screenWordsLeave() {}
