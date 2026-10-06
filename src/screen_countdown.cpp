// Theme "countdown": the days left until a date (and optionally a time) set in the Time tab,
// under a short label. The last 24 hours switch to HH:MM:SS; once the moment has come the
// screen says "Reached!" and how long ago.
//
//   y  14..40    label (Font 4, or Font 2 if it is too wide)
//   y  60..135   the number of days (Font 8; Font 7 for five digits) / HH:MM:SS on the last day
//   y 142..168   "days left" / "left"
//   y 188..214   the target date (and time) in the chosen date format
//   y 220..236   a hint when the clock is not usable yet
//
// Update(false) repaints one thing at a time: the number when it changes (once a day, or every
// second on the last day), nothing otherwise. The target is local time as the clock shows it.
#include "bigfont.h"
#include "config.h"
#include "countdown_calc.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"

namespace {

const int16_t LABEL_Y = 14;
const int16_t NUMBER_Y = 60;
const int16_t CAPTION_Y = 142;
const int16_t DATE_Y = 188;
const int16_t HINT_Y = 220;

enum Mode : int8_t { M_NONE = -1, M_NO_TARGET, M_NO_CLOCK, M_DAYS, M_LAST_DAY, M_REACHED };

int8_t shownMode;
int32_t shownValue;          // days, or seconds left on the last day, or days since
int8_t shownHint;            // 0 none, 1 syncing, 2 no network, 3 UTC notice
bool staticDrawn;            // label and target line are on the screen

void resetShown() {
  shownMode = M_NONE;
  shownValue = -1;
  shownHint = -1;
  staticDrawn = false;
}

void drawText(const char *text, int16_t y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  tft.drawString(text, 120, y, font);
  tft.setTextPadding(0);
}

void clearBelowLabel() { tft.fillRect(0, NUMBER_Y - 4, config::SCREEN_W, DATE_Y - NUMBER_Y, TFT_BLACK); }

// The label (or "Countdown") and the target line. Drawn once per repaint.
void drawStatic() {
  const settings::Settings &s = settings::get();
  display::drawFit(s.cdLabel[0] ? s.cdLabel : "Countdown", LABEL_Y, TFT_YELLOW);
  if (s.cdYear) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = s.cdYear - 1900;
    t.tm_mon = s.cdMonth - 1;
    t.tm_mday = s.cdDay;
    char line[40];
    snprintf(line, sizeof(line), "%s ", timekeeping::weekdayName(countdown::weekday(s.cdYear, s.cdMonth, s.cdDay), false));
    timekeeping::formatDate(line + strlen(line), sizeof(line) - strlen(line), t);
    if (s.cdMinutes) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %02u:%02u", s.cdMinutes / 60, s.cdMinutes % 60);
    display::drawFit(line, DATE_Y, TFT_LIGHTGREY);
  }
  staticDrawn = true;
}

void drawHint(int8_t kind) {
  const char *text = "";
  if (kind == 1) text = "Syncing the time...";
  else if (kind == 2) text = "No network - time not available";
  else if (kind == 3) text = "UTC - set a city or an offset";
  drawText(text, HINT_Y, 2, TFT_DARKGREY);
  shownHint = kind;
}

void drawDays(int32_t days) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%ld", (long)days);
  bool big = strlen(buf) <= 4;
  tft.fillRect(0, NUMBER_Y - 4, config::SCREEN_W, CAPTION_Y - NUMBER_Y + 4, TFT_BLACK);   // a 5-digit number replaces a 4-digit one
  drawText(buf, big ? NUMBER_Y : NUMBER_Y + 14, big ? bigfont::ID : 7, TFT_WHITE);
  drawText(days == 1 ? "day left" : "days left", CAPTION_Y, 4, TFT_LIGHTGREY);
}

void drawLastDay(int32_t seconds, bool full) {
  char buf[24];
  if (full) {
    clearBelowLabel();
    drawStatic();
    drawText("left", CAPTION_Y, 4, TFT_LIGHTGREY);
  }
  snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld", (long)(seconds / 3600), (long)(seconds / 60 % 60), (long)(seconds % 60));
  drawText(buf, NUMBER_Y + 14, 7, TFT_WHITE);
}

void drawReached(int32_t daysAgo) {
  clearBelowLabel();
  drawText("Reached!", NUMBER_Y + 10, 4, TFT_GREEN);
  if (daysAgo >= 1) {
    char buf[24];
    snprintf(buf, sizeof(buf), daysAgo == 1 ? "%ld day ago" : "%ld days ago", (long)daysAgo);
    drawText(buf, NUMBER_Y + 56, 4, TFT_LIGHTGREY);
  }
}

}  // namespace

void screenCountdownEnter() { resetShown(); }

void screenCountdownUpdate(bool full) {
  const settings::Settings &s = settings::get();
  if (full) resetShown();
  if (!staticDrawn) drawStatic();

  if (!s.cdYear) {
    if (shownMode != M_NO_TARGET) {
      clearBelowLabel();
      display::drawMessage("No date set", "Set one in the Time tab of the web UI");
      shownMode = M_NO_TARGET;
    }
    return;
  }

  struct tm t;
  if (!timekeeping::localTime(t)) {
    if (shownMode != M_NO_CLOCK) {
      clearBelowLabel();
      shownMode = M_NO_CLOCK;
      shownHint = -1;
    }
    int8_t hint = net::isAp() ? 2 : 1;
    if (hint != shownHint) drawHint(hint);
    return;
  }

  int64_t now = countdown::naiveSeconds(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  int64_t left = countdown::naiveSeconds(s.cdYear, s.cdMonth, s.cdDay, 0, 0, 0) + (int64_t)s.cdMinutes * 60 - now;

  Mode mode;
  int32_t value;
  if (left <= 0) {
    mode = M_REACHED;
    value = (int32_t)((-left) / 86400);
  } else if (left < 86400) {
    mode = M_LAST_DAY;
    value = (int32_t)left;
  } else {
    mode = M_DAYS;
    value = (int32_t)(left / 86400);
  }

  if (mode != shownMode || value != shownValue) {
    bool modeChanged = mode != shownMode;
    switch (mode) {
      case M_DAYS: drawDays(value); break;
      case M_LAST_DAY: drawLastDay(value, modeChanged); break;
      default: drawReached(value); break;
    }
    shownMode = mode;
    shownValue = value;
  }

  int8_t hint = timekeeping::offsetKnown() ? 0 : 3;
  if (hint != shownHint) drawHint(hint);
}

void screenCountdownLeave() {}
