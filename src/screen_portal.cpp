// Theme "portal" (Status-Portal): how the services are doing, at a glance.
//
//   y   0..35   banner: the overall status as a coloured band (red DOWN, orange DEGRADED, blue
//               MAINTENANCE, yellow SLOW, green OPERATIONAL)
//   y  41..57   counts per state: OK 5  SLOW 1  DEGR 1  DOWN 2 (only the states that have any)
//   y  62..     rows of 20 px, eight of them (seven when there is a ticker): the services that are not
//               operational, then the open incidents, then the maintenance in progress or coming up.
//               Each group gets rows in turn until they run out; a group that does not fit ends in a
//               "+N more" row
//   y 220..236  announcements: one line, or a slow ticker when it does not fit
//
// Every block is left out when its switch is off in the web interface or when there is nothing in it.
// With no fresh answer the screen shows a notice instead (portal_ui.h): not set up, no network,
// contacting, or why the portal does not answer.
//
// Flicker: nothing is cleared to repaint it. A row is redrawn only when its text, colour or tag
// changed, with the text padded so the old one is covered; the banner and the counts redraw only when the
// overall status or the counts change. Update(false) does nothing at all while nothing changed.
#include "config.h"
#include "display.h"
#include "portal.h"
#include "portal_ui.h"
#include "settings.h"

namespace {

using portal::Summary;

const int16_t BANNER_H = 36;
const int16_t COUNTS_Y = 41, COUNTS_H = 18;
const int16_t ROWS_Y = 62, ROW_H = 20;
const uint8_t ROWS_MAX = 8;
const uint8_t ROWS_WITH_TICKER = 7;
const int16_t TICKER_Y = 220;
const int16_t GLYPH_W = 16;                // the cell left of the text: a dot, "!" or "M"
const int16_t TEXT_X = 18, TEXT_W = 152;
const int16_t TAG_RIGHT = 238, TAG_W = 66;

enum Glyph : uint8_t { G_NONE = 0, G_DOT, G_BANG, G_MAINT };

struct Row {
  char text[28];
  char tag[10];
  uint16_t color;      // dot / glyph
  uint16_t tagColor;
  uint8_t glyph;
};

Row rows[ROWS_MAX];                        // what the screen should show now
uint8_t rowCount;
uint32_t shownSig[ROWS_MAX];               // what is on the screen, row by row
uint8_t shownRows;

int8_t shownNoticeKind;                    // portal_ui::Kind on the screen, -1 = content (or nothing yet)
uint32_t shownNoticeKey;
int16_t shownBanner;                       // portal::Status in the banner, -1 = nothing
uint32_t shownCounts;                      // signature of the counts line, 0 = nothing
uint32_t shownUpdated;                     // portal::updatedAtMs() the rows were built from
uint32_t shownMinute;                      // the portal minute they were built in (ages and countdowns)
bool shownTicker;
bool emptyMessageShown;                    // "All clear" is on the screen

// The ticker. `tickerText` holds the announcements joined on one line; it scrolls when it is wider than the
// band, one character at a time (a smooth pixel scroll would mean redrawing the band 25 times a second).
char tickerText[200];
bool tickerHas;                            // there is something to show in the band
uint32_t tickerHash;                       // of the text and its colour: the scroll goes on across refreshes
uint16_t tickerLen;
int16_t tickerWidth;
uint16_t tickerColor;
bool tickerScrolls;
uint16_t tickerPos;
int32_t tickerShownPos;                    // -1 = nothing drawn yet
uint32_t tickerStepAt;
const uint32_t TICKER_HOLD_MS = 3000;      // at the start of every pass
const uint32_t TICKER_STEP_MS = 230;
const int16_t TICKER_MAX_W = 236;

void resetContent() {
  emptyMessageShown = false;
  tickerHas = false;
  tickerHash = 0;
  shownRows = 0;
  memset(shownSig, 0, sizeof(shownSig));
  shownBanner = -1;
  shownCounts = 0;
  shownUpdated = 0;
  shownMinute = 0;
  shownTicker = false;
  tickerShownPos = -1;
}

void resetShown() {
  shownNoticeKind = -1;
  shownNoticeKey = 0;
  resetContent();
}

uint32_t hashBytes(uint32_t h, const void *data, size_t n) {
  const uint8_t *p = (const uint8_t *)data;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h;
}

uint32_t rowSignature(const Row &r) {
  uint32_t h = 2166136261u;
  h = hashBytes(h, r.text, strlen(r.text) + 1);
  h = hashBytes(h, r.tag, strlen(r.tag) + 1);
  h = hashBytes(h, &r.color, sizeof(r.color));
  h = hashBytes(h, &r.tagColor, sizeof(r.tagColor));
  h = hashBytes(h, &r.glyph, sizeof(r.glyph));
  return h ? h : 1;
}

// --- Banner and counts ---------------------------------------------------------------------------------

const char *bannerWord(uint8_t status) {
  switch (status) {
    case portal::ST_DOWN: return "DOWN";
    case portal::ST_DEGRADED: return "DEGRADED";
    case portal::ST_MAINTENANCE: return "MAINTENANCE";
    case portal::ST_SLOW: return "SLOW";
    default: return "OPERATIONAL";
  }
}

void drawBanner(uint8_t status) {
  uint16_t fill = status == portal::ST_DOWN ? portal_ui::RED
                : status == portal::ST_DEGRADED ? portal_ui::ORANGE
                : status == portal::ST_MAINTENANCE ? portal_ui::BLUE
                : status == portal::ST_SLOW ? portal_ui::YELLOW
                                            : (uint16_t)0x0400;   // dark green
  uint16_t ink = (status == portal::ST_DEGRADED || status == portal::ST_SLOW) ? (uint16_t)TFT_BLACK : (uint16_t)TFT_WHITE;
  tft.fillRect(0, 0, config::SCREEN_W, BANNER_H, fill);
  display::drawFit(bannerWord(status), 5, ink, fill);
}

struct Segment {
  char text[14];
  uint16_t color;
};

// The states that have any service in them, as "OK 5", "SLOW 1", ... Returns how many were written.
// When the words do not fit the width of the screen (hundreds of services, all five states in use) only the
// numbers are written, still in their colours. `gap` is the space to leave between two of them.
uint8_t countSegments(const Summary &d, Segment *out, int16_t &gap) {
  struct Entry {
    const char *word;
    uint16_t n;
    uint16_t color;
  } entries[] = {{"OK", d.services.operational, portal_ui::GREEN},
                 {"SLOW", d.services.slow, portal_ui::YELLOW},
                 {"MAINT", d.services.maintenance, portal_ui::LIGHT_BLUE},
                 {"DEGR", d.services.degraded, portal_ui::ORANGE},
                 {"DOWN", d.services.down, portal_ui::RED}};
  uint8_t n = 0;
  for (bool words = true;; words = false) {
    n = 0;
    int16_t total = 0;
    for (const Entry &e : entries) {
      if (e.n == 0) continue;
      if (words) snprintf(out[n].text, sizeof(out[n].text), "%s %u", e.word, (unsigned)e.n);
      else snprintf(out[n].text, sizeof(out[n].text), "%u", (unsigned)e.n);
      out[n].color = e.color;
      total += tft.textWidth(out[n].text, 2);
      n++;
    }
    gap = 12;
    if (n > 1 && total + gap * (n - 1) > TICKER_MAX_W) gap = words ? 5 : 10;
    if (!words || n <= 1 || total + gap * (n - 1) <= TICKER_MAX_W) break;
  }
  return n;
}

void drawCounts(const Summary &d) {
  tft.fillRect(0, COUNTS_Y, config::SCREEN_W, COUNTS_H, TFT_BLACK);
  if (!d.services.present) return;
  Segment seg[5];
  int16_t gap;
  uint8_t n = countSegments(d, seg, gap);
  int16_t total = gap * (n > 0 ? n - 1 : 0);
  for (uint8_t i = 0; i < n; i++) total += tft.textWidth(seg[i].text, 2);
  int16_t x = (config::SCREEN_W - total) / 2;
  if (x < 2) x = 2;
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  for (uint8_t i = 0; i < n; i++) {
    tft.setTextColor(seg[i].color, TFT_BLACK);
    x += tft.drawString(seg[i].text, x, COUNTS_Y + 1, 2) + gap;
  }
}

uint32_t countsSignature(const Summary &d) {
  if (!d.services.present) return 1;
  Segment seg[5];
  int16_t gap;
  uint8_t n = countSegments(d, seg, gap);
  uint32_t h = 2166136261u;
  for (uint8_t i = 0; i < n; i++) h = hashBytes(h, seg[i].text, strlen(seg[i].text) + 1);
  return h ? h : 2;
}

// --- Rows --------------------------------------------------------------------------------------------------

// Adds a row, if there is room.
Row *addRow(uint8_t capacity, uint8_t glyph, uint16_t color, const char *text, const char *tag, uint16_t tagColor) {
  if (rowCount >= capacity) return nullptr;
  Row &r = rows[rowCount++];
  memset(&r, 0, sizeof(r));
  portal_ui::fitText(r.text, sizeof(r.text), text, TEXT_W, 2);
  strlcpy(r.tag, tag, sizeof(r.tag));
  r.color = color;
  r.tagColor = tagColor;
  r.glyph = glyph;
  return &r;
}

void addMore(uint8_t capacity, uint16_t n, const char *what) {
  char text[24];
  snprintf(text, sizeof(text), "+%u more %s", (unsigned)n, what);
  addRow(capacity, G_NONE, 0, text, "", 0);
}

// How many rows each group gets: in turn, one at a time, in priority order, until the rows run out.
// A group that cannot show everything gives its last row to the "+N more" line.
void allocate(const uint8_t demand[3], uint8_t capacity, uint8_t out[3]) {
  uint8_t used = 0;
  out[0] = out[1] = out[2] = 0;
  for (uint8_t level = 1; level <= ROWS_MAX && used < capacity; level++) {
    for (uint8_t g = 0; g < 3 && used < capacity; g++) {
      if (out[g] < demand[g] && out[g] < level) {
        out[g]++;
        used++;
      }
    }
  }
}

void buildRows(const Summary &d, uint8_t capacity, uint32_t now) {
  rowCount = 0;
  char tag[10];

  // What each group would like to show: its items, and a "+N more" row for what the portal left out.
  uint16_t hiddenServices = 0, hiddenIncidents = 0, hiddenMaint = 0;
  if (d.services.present) {
    uint16_t notOk = d.services.total > d.services.operational ? d.services.total - d.services.operational : 0;
    hiddenServices = notOk > d.services.n ? notOk - d.services.n : 0;
  }
  if (d.incidents.present) hiddenIncidents = d.incidents.open > d.incidents.n ? d.incidents.open - d.incidents.n : 0;
  uint16_t maintTotal = d.maintenance.present ? d.maintenance.active + d.maintenance.upcoming : 0;
  if (d.maintenance.present) hiddenMaint = maintTotal > d.maintenance.n ? maintTotal - d.maintenance.n : 0;

  uint8_t demand[3] = {(uint8_t)((d.services.present ? d.services.n : 0) + (hiddenServices ? 1 : 0)),
                       (uint8_t)((d.incidents.present ? d.incidents.n : 0) + (hiddenIncidents ? 1 : 0)),
                       (uint8_t)((d.maintenance.present ? d.maintenance.n : 0) + (hiddenMaint ? 1 : 0))};
  uint8_t give[3];
  allocate(demand, capacity, give);

  // Services.
  if (give[0]) {
    uint8_t items = d.services.n;
    bool more = hiddenServices > 0 || give[0] < demand[0];
    uint8_t shownItems = more ? give[0] - 1 : give[0];
    if (shownItems > items) shownItems = items;
    for (uint8_t i = 0; i < shownItems; i++) {
      const portal::Service &s = d.services.items[i];
      addRow(capacity, G_DOT, portal_ui::statusColor(s.status), s.name, portal_ui::statusTag(s.status), portal_ui::statusColor(s.status));
    }
    if (more) {
      uint16_t notOk = d.services.total > d.services.operational ? d.services.total - d.services.operational : 0;
      addMore(capacity, notOk > shownItems ? notOk - shownItems : 1, "services");
    }
  }

  // Open incidents: the time since they started on the right, coloured by how far along they are.
  if (give[1]) {
    bool more = hiddenIncidents > 0 || give[1] < demand[1];
    uint8_t shownItems = more ? give[1] - 1 : give[1];
    if (shownItems > d.incidents.n) shownItems = d.incidents.n;
    for (uint8_t i = 0; i < shownItems; i++) {
      const portal::Incident &inc = d.incidents.items[i];
      tag[0] = '\0';
      if (now && inc.since && now >= inc.since) portal_ui::formatSpan(tag, sizeof(tag), now - inc.since);
      uint16_t color = inc.status == portal::INC_MONITORING ? portal_ui::YELLOW
                     : inc.status == portal::INC_IDENTIFIED ? portal_ui::ORANGE
                                                            : portal_ui::RED;
      addRow(capacity, G_BANG, color, inc.title, tag, color);
    }
    if (more) addMore(capacity, d.incidents.open > shownItems ? d.incidents.open - shownItems : 1, "incidents");
  }

  // Maintenance: "38m left" while it runs, "in 2d" before it starts.
  if (give[2]) {
    bool more = hiddenMaint > 0 || give[2] < demand[2];
    uint8_t shownItems = more ? give[2] - 1 : give[2];
    if (shownItems > d.maintenance.n) shownItems = d.maintenance.n;
    for (uint8_t i = 0; i < shownItems; i++) {
      const portal::Maintenance &m = d.maintenance.items[i];
      char span[10];
      span[0] = '\0';
      if (m.active) {
        if (now && m.ends > now) {
          portal_ui::formatSpan(span, sizeof(span), m.ends - now);
          snprintf(tag, sizeof(tag), "%s left", span);
        } else {
          strlcpy(tag, "now", sizeof(tag));
        }
      } else {
        if (now && m.starts > now) {
          portal_ui::formatSpan(span, sizeof(span), m.starts - now);
          snprintf(tag, sizeof(tag), "in %s", span);
        } else {
          strlcpy(tag, "soon", sizeof(tag));
        }
      }
      addRow(capacity, G_MAINT, m.active ? portal_ui::LIGHT_BLUE : portal_ui::GREY, m.title, tag,
             m.active ? portal_ui::LIGHT_BLUE : portal_ui::GREY);
    }
    if (more) addMore(capacity, maintTotal > shownItems ? maintTotal - shownItems : 1, "maintenance");
  }
}

void drawRow(uint8_t index) {
  const Row &r = rows[index];
  int16_t y = ROWS_Y + index * ROW_H;
  tft.fillRect(0, y, GLYPH_W, ROW_H, TFT_BLACK);   // the glyph cell: the old row may have had another kind
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(r.color, TFT_BLACK);
  if (r.glyph == G_DOT) tft.fillCircle(7, y + 10, 4, r.color);
  else if (r.glyph == G_BANG) tft.drawString("!", 7, y + 2, 2);
  else if (r.glyph == G_MAINT) tft.drawString("M", 7, y + 2, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(TEXT_W);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(r.text, TEXT_X, y + 2, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(TAG_W);
  tft.setTextColor(r.tagColor, TFT_BLACK);
  tft.drawString(r.tag, TAG_RIGHT, y + 2, 2);
  tft.setTextPadding(0);
}

// Draws the rows that differ from what is on the screen, and blanks the ones that are gone.
void drawRows() {
  for (uint8_t i = 0; i < rowCount; i++) {
    uint32_t sig = rowSignature(rows[i]);
    if (i < shownRows && shownSig[i] == sig) continue;
    drawRow(i);
    shownSig[i] = sig;
  }
  if (shownRows > rowCount) {
    tft.fillRect(0, ROWS_Y + rowCount * ROW_H, config::SCREEN_W, (shownRows - rowCount) * ROW_H, TFT_BLACK);
  }
  shownRows = rowCount;
}

// Nothing to list while everything is fine: say so, instead of leaving a black hole under the counts.
// The words are drawn once, and removed by the first row that appears.
void drawEmptyMessage(bool show) {
  if (show == emptyMessageShown) return;
  emptyMessageShown = show;
  if (show) {
    display::drawFit("All clear", 112, portal_ui::GREEN);
    display::drawFit("Nothing to report", 146, portal_ui::GREY);
  } else {
    tft.fillRect(0, 100, config::SCREEN_W, 70, TFT_BLACK);
  }
}

// --- Announcements ------------------------------------------------------------------------------------------

// Joins the announcements on one line ("Title: text  |  Title: text") and finds the colour of the
// most serious one. The scroll starts over only when the words or the colour are not the ones that were
// already on their way: a refresh every minute must not send the ticker back to its beginning.
bool buildTicker(const Summary &d) {
  tickerText[0] = '\0';
  uint16_t color = portal_ui::LIGHT_GREY;
  if (d.announcements.present && d.announcements.n > 0) {
    uint8_t worst = portal::ANN_INFO;
    for (uint8_t i = 0; i < d.announcements.n; i++) {
      const portal::Announcement &a = d.announcements.items[i];
      if (i) strlcat(tickerText, "  |  ", sizeof(tickerText));
      if (a.title[0]) {
        strlcat(tickerText, a.title, sizeof(tickerText));
        if (a.text[0]) strlcat(tickerText, ": ", sizeof(tickerText));
      }
      strlcat(tickerText, a.text, sizeof(tickerText));
      if (a.type == portal::ANN_CRITICAL) worst = portal::ANN_CRITICAL;
      else if (a.type == portal::ANN_WARNING && worst != portal::ANN_CRITICAL) worst = portal::ANN_WARNING;
      else if (a.type == portal::ANN_SUCCESS && worst == portal::ANN_INFO) worst = portal::ANN_SUCCESS;
    }
    color = worst == portal::ANN_CRITICAL ? portal_ui::RED
          : worst == portal::ANN_WARNING ? portal_ui::ORANGE
          : worst == portal::ANN_SUCCESS ? portal_ui::GREEN
                                         : portal_ui::LIGHT_GREY;
  }
  tickerLen = (uint16_t)strlen(tickerText);
  if (tickerLen == 0) return false;

  uint32_t h = hashBytes(2166136261u, tickerText, tickerLen);
  h = hashBytes(h, &color, sizeof(color));
  tickerColor = color;
  tickerWidth = tft.textWidth(tickerText, 2);
  tickerScrolls = tickerWidth > TICKER_MAX_W;
  if (h != tickerHash || !tickerHas) {
    tickerHash = h;
    tickerPos = 0;
    tickerShownPos = -1;
    tickerStepAt = millis() + TICKER_HOLD_MS;
  }
  return true;
}

// The window of the scrolling text that starts at character `pos`: the text goes round, with a gap.
void tickerWindow(char *out, size_t cap, uint16_t pos) {
  const uint16_t cycle = tickerLen + 5;
  size_t n = 0;
  while (n + 1 < cap) {
    uint16_t at = (pos + n) % cycle;
    out[n] = at < tickerLen ? tickerText[at] : ' ';
    out[n + 1] = '\0';
    if (tft.textWidth(out, 2) > TICKER_MAX_W) break;
    n++;
  }
  out[n] = '\0';
}

// Draws the band when it is not on the screen yet, and steps the scroll when it is time.
void drawTicker() {
  const bool first = tickerShownPos < 0;
  if (!tickerScrolls && !first) return;
  tft.setTextColor(tickerColor, TFT_BLACK);
  tft.setTextPadding(config::SCREEN_W - 2);
  if (!tickerScrolls) {
    tft.setTextDatum(TC_DATUM);
    tft.drawString(tickerText, config::SCREEN_W / 2, TICKER_Y, 2);
    tickerShownPos = 0;
  } else {
    uint32_t now = millis();
    if (!first && (int32_t)(now - tickerStepAt) < 0) {
      tft.setTextPadding(0);
      return;
    }
    if (!first) {
      tickerPos = (uint16_t)((tickerPos + 1) % (tickerLen + 5));
      tickerStepAt = now + (tickerPos == 0 ? TICKER_HOLD_MS : TICKER_STEP_MS);
    }
    char window[80];
    tickerWindow(window, sizeof(window), tickerPos);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(window, 1, TICKER_Y, 2);
    tickerShownPos = tickerPos;
  }
  tft.setTextPadding(0);
}

void clearTicker() { tft.fillRect(0, TICKER_Y - 2, config::SCREEN_W, 20, TFT_BLACK); }

void updateContent(bool leftNotice) {
  const Summary &d = portal::data();
  const uint32_t now = portal::nowEpoch();

  if (leftNotice) tft.fillScreen(TFT_BLACK);   // the notice's words are still there
  if (shownBanner != d.overall) {
    drawBanner(d.overall);
    shownBanner = d.overall;
  }
  uint32_t counts = countsSignature(d);
  if (counts != shownCounts) {
    drawCounts(d);
    shownCounts = counts;
  }

  const uint32_t updated = portal::updatedAtMs();
  const uint32_t minute = now / 60;
  const bool newData = updated != shownUpdated;
  if (newData) tickerHas = buildTicker(d);   // every answer: the words may have changed
  if (newData || minute != shownMinute || tickerHas != shownTicker) {
    buildRows(d, tickerHas ? ROWS_WITH_TICKER : ROWS_MAX, now);
    const bool empty = rowCount == 0 && d.overall <= portal::ST_SLOW && d.services.present;
    if (!empty) drawEmptyMessage(false);   // before the rows: it clears a band they may be about to use
    drawRows();
    if (empty) drawEmptyMessage(true);
    if (!tickerHas && shownTicker) clearTicker();
    shownUpdated = updated;
    shownMinute = minute;
    shownTicker = tickerHas;
  }
  if (tickerHas) drawTicker();
}

}  // namespace

void screenPortalEnter() { resetShown(); }

void screenPortalUpdate(bool full) {
  if (full) resetShown();
  portal_ui::Kind kind = portal_ui::notice();
  if (kind != portal_ui::NONE) {
    uint32_t key = portal_ui::noticeKey(kind);
    if (shownNoticeKind != (int8_t)kind || shownNoticeKey != key) {
      tft.fillScreen(TFT_BLACK);
      portal_ui::drawNotice(kind);
      resetContent();
      shownNoticeKind = (int8_t)kind;
      shownNoticeKey = key;
    }
    return;
  }
  bool leftNotice = shownNoticeKind >= 0;
  if (leftNotice) {
    resetContent();
    shownNoticeKind = -1;
  }
  updateContent(leftNotice);
}

void screenPortalLeave() {}
