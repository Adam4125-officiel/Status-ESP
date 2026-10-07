#include "portal_ui.h"

#include "config.h"
#include "display.h"
#include "net.h"
#include "portal.h"

namespace portal_ui {

uint16_t statusColor(uint8_t status) {
  switch (status) {
    case portal::ST_DOWN: return RED;
    case portal::ST_DEGRADED: return ORANGE;
    case portal::ST_MAINTENANCE: return LIGHT_BLUE;
    case portal::ST_SLOW: return YELLOW;
    case portal::ST_OPERATIONAL: return GREEN;
    default: return GREY;
  }
}

const char *statusTag(uint8_t status) {
  switch (status) {
    case portal::ST_DOWN: return "DOWN";
    case portal::ST_DEGRADED: return "DEGRADED";
    case portal::ST_MAINTENANCE: return "MAINT";
    case portal::ST_SLOW: return "SLOW";
    case portal::ST_OPERATIONAL: return "OK";
    default: return "";
  }
}

uint16_t severityColor(uint8_t severity) {
  return severity == portal::SEV_CRIT ? RED : (severity == portal::SEV_WARN ? ORANGE : GREEN);
}

Kind notice() {
  if (!portal::configured()) return NOT_CONFIGURED;
  if (portal::fresh()) return NONE;
  if (!net::isConnected()) return NO_NETWORK;
  const portal::Diag &d = portal::diag();
  return (d.attemptMs && d.error[0]) ? UNREACHABLE : LOADING;
}

uint32_t noticeKey(Kind kind) {
  uint32_t key = kind;
  if (kind == UNREACHABLE) {   // FNV-1a of the reason: it is what the screen shows
    uint32_t h = 2166136261u;
    for (const char *p = portal::diag().error; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    key = (h << 3) ^ kind;
  }
  return key;
}

// Wraps `text` over at most maxLines centred lines of Font 2, breaking at spaces where it can.
static void drawWrapped(const char *text, int16_t y, uint8_t maxLines, uint16_t color) {
  const int16_t maxW = 226;
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  char line[48];
  for (uint8_t i = 0; i < maxLines && *text; i++, y += 18) {
    size_t len = 0;   // the longest prefix that fits
    while (text[len] && len < sizeof(line) - 1) {
      memcpy(line, text, len + 1);
      line[len + 1] = '\0';
      if (tft.textWidth(line, 2) > maxW) break;
      len++;
    }
    if (len == 0) len = 1;
    if (text[len]) {   // more follows: back up to the last space inside the line, if there is one
      size_t cut = len;
      while (cut > 0 && text[cut - 1] != ' ') cut--;
      if (cut > 0) len = cut;
    }
    memcpy(line, text, len);
    line[len] = '\0';
    while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
    tft.drawString(line, 120, y, 2);
    text += strlen(line);
    while (*text == ' ') text++;
  }
  tft.setTextPadding(0);
}

void drawNotice(Kind kind) {
  switch (kind) {
    case NOT_CONFIGURED:
      display::drawMessage("Portal not set up", "Set it up in the web UI");
      break;
    case NO_NETWORK:
      display::drawMessage("No network", "Status-Portal needs Wi-Fi", TFT_YELLOW);
      break;
    case LOADING:
      display::drawMessage("Contacting portal", "Please wait...");
      break;
    case UNREACHABLE:
      display::drawMessage("No portal data", "", TFT_YELLOW);   // the reason is longer than one line: wrapped below
      drawWrapped(portal::diag().error, 132, 3, TFT_LIGHTGREY);
      break;
    default:
      break;
  }
}

void fitText(char *out, size_t cap, const char *text, int16_t maxW, uint8_t font) {
  strlcpy(out, text, cap);
  if (tft.textWidth(out, font) <= maxW) return;
  size_t len = strlen(out);
  const int16_t dots = tft.textWidth("...", font);
  while (len > 1 && tft.textWidth(out, font) + dots > maxW) out[--len] = '\0';
  while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
  strlcat(out, "...", cap);
}

void formatSpan(char *out, size_t cap, uint32_t seconds) {
  uint32_t minutes = seconds / 60;
  if (minutes < 1) snprintf(out, cap, "<1m");
  else if (minutes < 60) snprintf(out, cap, "%um", (unsigned)minutes);
  else if (minutes < 600) snprintf(out, cap, "%uh%02um", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
  else if (minutes < 2880) snprintf(out, cap, "%uh", (unsigned)(minutes / 60));
  else snprintf(out, cap, "%ud", (unsigned)(minutes / 1440));
}

void jellyfinLine(const portal::Summary &d, char *out, size_t cap) {
  out[0] = '\0';
  if (!portal::jellyfinBusy(d)) return;
  const auto &j = d.jellyfin;
  char what[56];
  if (j.transcodes) {
    // "2 transcodes", and how many tasks run beside them.
    int n = snprintf(what, sizeof(what), "%u transcode%s", (unsigned)j.transcodes, j.transcodes == 1 ? "" : "s");
    if (j.taskN && n > 0 && (size_t)n < sizeof(what)) snprintf(what + n, sizeof(what) - n, ", %u task%s", (unsigned)j.taskN, j.taskN == 1 ? "" : "s");
  } else {
    // Only tasks: the first one's name tells more than a count, and "+1" says there are others.
    int n = snprintf(what, sizeof(what), "%s", j.task[0]);
    if (j.taskN > 1 && n > 0 && (size_t)n < sizeof(what)) snprintf(what + n, sizeof(what) - n, " +%u", (unsigned)(j.taskN - 1));
  }
  char line[72];
  snprintf(line, sizeof(line), "Jellyfin  %s", what);
  fitText(out, cap, line, config::SCREEN_W - 8, 2);
}

void drawBand(int16_t y, int16_t h, uint16_t fill, uint16_t ink, const char *text) {
  tft.fillRect(0, y, config::SCREEN_W, h, fill);
  tft.setTextColor(ink, fill);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(text, config::SCREEN_W / 2, y + (h - 16) / 2, 2);
}

}  // namespace portal_ui
