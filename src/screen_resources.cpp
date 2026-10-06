// Theme "resources" (Status-Portal): the load of the machine the portal runs on.
//
//   y   0..27   header: the portal's site name, or a red band "HIGH LOAD" while the CPU or the
//               memory is critical (the portal's own judgement, 85 % and up)
//   y  32..     six blocks of 30 px: CPU, RAM, then up to four disks, the fullest first. Each is a
//               label on the left, the percentage on the right and a bar under them, coloured by the
//               portal's own severity (green, orange from 60 %, red from 85 %)
//   y 218..234  network: "up 0.1  down 1.4 MB/s", when the portal reports it
//
// The portal sends these numbers whatever its own public pages show; this screen is gated by the
// "server's CPU, memory and disks" switch in the web interface (which also decides whether they are
// asked for at all). With no fresh answer the screen shows the notice of portal_ui.h.
//
// Flicker: a block is redrawn only when its label, percentage or severity changed, and a bar is drawn as
// two rectangles (the filled part, the empty part) that together cover it, so no pixel goes through black.
#include "config.h"
#include "display.h"
#include "portal.h"
#include "portal_ui.h"
#include "settings.h"
#include "units.h"

namespace {

using portal::NA;

const int16_t HEADER_H = 28;
const int16_t BLOCK_Y = 32, BLOCK_H = 30;
const int16_t BAR_X = 4, BAR_W = 232, BAR_H = 8, BAR_DY = 19;
const int16_t LABEL_W = 176, VALUE_W = 52;   // the label on the left, the percentage on the right of a block
const int16_t NET_Y = 218;
const uint8_t BLOCKS = 6;                   // CPU, RAM, four disks

int8_t shownNoticeKind;                     // portal_ui::Kind or the two local ones, -1 = content
uint32_t shownNoticeKey;
int8_t shownHeader;                         // 0 = site name, 1 = HIGH LOAD, -1 = nothing
uint32_t shownHeaderSig;
uint32_t shownBlock[BLOCKS];                // signature of what each block shows, 0 = nothing
uint32_t shownNet;
uint32_t shownUpdated;

// The two notices that belong to this screen only.
enum LocalNotice : int8_t { L_OFF = 100, L_NONE = 101 };

void resetContent() {
  shownHeader = -1;
  shownHeaderSig = 0;
  memset(shownBlock, 0, sizeof(shownBlock));
  shownNet = 0;
  shownUpdated = 0;
}

void resetShown() {
  shownNoticeKind = -1;
  shownNoticeKey = 0;
  resetContent();
}

uint32_t hashBytes(uint32_t h, const void *data, size_t n) {
  const uint8_t *p = (const uint8_t *)data;
  while (n--) h = (h ^ *p++) * 16777619u;
  return h ? h : 1;
}

uint32_t hashText(uint32_t h, const char *s) { return hashBytes(h, s, strlen(s) + 1); }

// "9.8" for 98 tenths.
void formatTenths(char *out, size_t cap, uint16_t tenths) { snprintf(out, cap, "%u.%u", tenths / 10, tenths % 10); }

// "460 GB" or "1.2 TB".
void formatSize(char *out, size_t cap, uint16_t gb) {
  if (gb >= 1000) snprintf(out, cap, "%u.%u TB", gb / 1000, (gb % 1000) / 100);
  else snprintf(out, cap, "%u GB", gb);
}

// --- Header -----------------------------------------------------------------------------------------------------

bool highLoad(const portal::Summary &d) {
  return d.resources.present && (d.resources.cpuSev == portal::SEV_CRIT || d.resources.memSev == portal::SEV_CRIT);
}

void drawHeader(const portal::Summary &d) {
  if (highLoad(d)) {
    tft.fillRect(0, 0, config::SCREEN_W, HEADER_H, portal_ui::RED);
    display::drawFit("HIGH LOAD", 1, TFT_WHITE, portal_ui::RED);
  } else {
    tft.fillRect(0, 0, config::SCREEN_W, HEADER_H, TFT_BLACK);
    display::drawFit(d.site[0] ? d.site : "Resources", 1, portal_ui::LIGHT_GREY);
  }
}

// --- Blocks -------------------------------------------------------------------------------------------------------

struct Block {
  char label[40];
  char value[8];
  int16_t pct;        // NA = unknown
  uint8_t sev;
};

void drawBar(int16_t y, int16_t pct, uint16_t color) {
  int16_t filled = pct == NA ? 0 : (int16_t)((int32_t)pct * BAR_W / 100);
  if (filled > 0) tft.fillRect(BAR_X, y, filled, BAR_H, color);
  if (filled < BAR_W) tft.fillRect(BAR_X + filled, y, BAR_W - filled, BAR_H, portal_ui::TRACK);
}

void drawBlock(uint8_t index, const Block &b) {
  int16_t y = BLOCK_Y + index * BLOCK_H;
  uint16_t color = portal_ui::severityColor(b.sev);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(LABEL_W);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(b.label, BAR_X, y, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(VALUE_W);
  tft.setTextColor(b.pct == NA ? portal_ui::GREY : color, TFT_BLACK);
  tft.drawString(b.value, BAR_X + BAR_W, y, 2);
  tft.setTextPadding(0);
  drawBar(y + BAR_DY, b.pct, color);
}

uint32_t blockSignature(const Block &b) {
  uint32_t h = hashText(2166136261u, b.label);
  h = hashText(h, b.value);
  h = hashBytes(h, &b.pct, sizeof(b.pct));
  return hashBytes(h, &b.sev, sizeof(b.sev));
}

void makeValue(char *out, size_t cap, int16_t pct) {
  if (pct == NA) strlcpy(out, "--%", cap);
  else snprintf(out, cap, "%d%%", pct);
}

// Builds block `index` (0 CPU, 1 RAM, 2.. disks). False when there is no such block.
bool makeBlock(const portal::Summary &d, uint8_t index, Block &b) {
  memset(&b, 0, sizeof(b));
  const auto &r = d.resources;
  if (index == 0) {
    if (r.cpuTempC != NA) snprintf(b.label, sizeof(b.label), "CPU   %dC", r.cpuTempC);
    else strlcpy(b.label, "CPU", sizeof(b.label));
    b.pct = r.cpu;
    b.sev = r.cpuSev;
  } else if (index == 1) {
    char used[10], total[10];
    formatTenths(used, sizeof(used), r.memUsedDg);
    formatTenths(total, sizeof(total), r.memTotalDg);
    if (r.memTotalDg) snprintf(b.label, sizeof(b.label), "RAM   %s / %s GB", used, total);
    else strlcpy(b.label, "RAM", sizeof(b.label));
    b.pct = r.mem;
    b.sev = r.memSev;
  } else {
    uint8_t disk = index - 2;
    if (disk >= r.n) return false;
    // "Media   460 GB free": the name gives way, so that the whole label stays inside its padding.
    char name[20], size[12], tail[24];
    formatSize(size, sizeof(size), r.disks[disk].freeGb);
    snprintf(tail, sizeof(tail), "   %s free", size);
    portal_ui::fitText(name, sizeof(name), r.disks[disk].name[0] ? r.disks[disk].name : "disk", LABEL_W - tft.textWidth(tail, 2), 2);
    snprintf(b.label, sizeof(b.label), "%s%s", name, tail);
    b.pct = r.disks[disk].pct;
    b.sev = r.disks[disk].sev;
  }
  makeValue(b.value, sizeof(b.value), b.pct);
  return true;
}

// "up 0.1  down 1.4 MB/s"; empty when the portal does not know the rates.
void makeNetLine(const portal::Summary &d, char *out, size_t cap) {
  out[0] = '\0';
  const auto &r = d.resources;
  if (r.netUp < 0 && r.netDown < 0) return;
  char up[10] = "--", down[10] = "--";
  if (r.netUp >= 0) units::formatFixed(up, sizeof(up), r.netUp, r.netUp < 10 ? 1 : 0);
  if (r.netDown >= 0) units::formatFixed(down, sizeof(down), r.netDown, r.netDown < 10 ? 1 : 0);
  snprintf(out, cap, "up %s   down %s MB/s", up, down);
}

void drawNotice(const char *title, const char *detail) {
  tft.fillScreen(TFT_BLACK);
  display::drawMessage(title, detail, TFT_YELLOW);
}

void updateContent() {
  const portal::Summary &d = portal::data();
  const uint32_t updated = portal::updatedAtMs();
  if (updated == shownUpdated && shownHeader >= 0) return;   // nothing new: nothing to do

  // The answer is what the header and the blocks are made of.
  uint32_t headerSig = highLoad(d) ? 1 : hashText(2, d.site);
  if (shownHeader < 0 || headerSig != shownHeaderSig) {
    drawHeader(d);
    shownHeader = highLoad(d) ? 1 : 0;
    shownHeaderSig = headerSig;
  }
  for (uint8_t i = 0; i < BLOCKS; i++) {
    Block b;
    if (makeBlock(d, i, b)) {
      uint32_t sig = blockSignature(b);
      if (sig != shownBlock[i]) {
        drawBlock(i, b);
        shownBlock[i] = sig;
      }
    } else if (shownBlock[i]) {   // a disk that is gone
      tft.fillRect(0, BLOCK_Y + i * BLOCK_H, config::SCREEN_W, BLOCK_H, TFT_BLACK);
      shownBlock[i] = 0;
    }
  }
  char net[40];
  makeNetLine(d, net, sizeof(net));
  uint32_t netSig = hashText(3, net);
  if (netSig != shownNet) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(config::SCREEN_W - 2);
    tft.setTextColor(portal_ui::GREY, TFT_BLACK);
    tft.drawString(net, config::SCREEN_W / 2, NET_Y, 2);
    tft.setTextPadding(0);
    shownNet = netSig;
  }
  shownUpdated = updated;
}

}  // namespace

void screenResourcesEnter() { resetShown(); }

void screenResourcesUpdate(bool full) {
  if (full) resetShown();

  // Why there is nothing to show, most general reason first: the portal's own states, then the two
  // that only this screen has.
  int8_t kind = (int8_t)portal_ui::notice();
  uint32_t key = 0;
  const char *title = nullptr, *detail = nullptr;
  if (kind != portal_ui::NONE) {
    key = portal_ui::noticeKey((portal_ui::Kind)kind);
  } else if (!(settings::get().portalSections & portal::SEC_RESOURCES)) {
    kind = L_OFF;
    title = "Resources are off";
    detail = "Tick them in the web UI";
  } else if (!portal::data().resources.present) {
    kind = L_NONE;
    title = "No resource data";
    detail = "The portal could not read them";
  }

  if (kind != portal_ui::NONE) {
    if (shownNoticeKind != kind || shownNoticeKey != key) {
      if (title) {
        drawNotice(title, detail);
      } else {
        tft.fillScreen(TFT_BLACK);
        portal_ui::drawNotice((portal_ui::Kind)kind);
      }
      resetContent();
      shownNoticeKind = kind;
      shownNoticeKey = key;
    }
    return;
  }
  if (shownNoticeKind >= 0) {   // the notice's words are still on the screen
    tft.fillScreen(TFT_BLACK);
    resetContent();
    shownNoticeKind = -1;
  }
  updateContent();
}

void screenResourcesLeave() {}
