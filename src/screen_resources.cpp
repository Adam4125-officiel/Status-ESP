// Theme "resources" (Status-Portal): the load of the machine the portal runs on.
//
//   y   0..27   header: the portal's site name, or a red band "HIGH LOAD" while the CPU or the
//               memory is critical (the portal's own judgement, 85 % and up)
//   y  32..     six blocks of 30 px per page: CPU, RAM, then each GPU (its load, then its video memory),
//               then the disks, the fullest first. Each is a label on the left, the percentage on the
//               right and a bar under them, coloured by the portal's own severity (green, orange from
//               60 %, red from 85 %). With more blocks than fit, they are split evenly over pages that
//               turn every portal_page seconds, as the Status-Portal screen's do
//   y 218..234  network: "up 0.1  down 1.4 MB/s", when the portal reports it; with several pages it
//               moves left to make room for "Page 2/3" on the right
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
const int16_t NET_PAGED_W = 160, PAGE_W = 70;   // the two texts of the footer when there are several pages
const uint8_t BLOCKS = 6;                   // blocks on one page

int8_t shownNoticeKind;                     // portal_ui::Kind or the two local ones, -1 = content
uint32_t shownNoticeKey;
int8_t shownHeader;                         // 0 = site name, 1 = HIGH LOAD, -1 = nothing
uint32_t shownHeaderSig;
uint32_t shownBlock[BLOCKS];                // signature of what each block shows, 0 = nothing
uint32_t shownNet;
int8_t shownPaged;                          // 1 = the footer is laid out for several pages, 0 = one, -1 = nothing
uint32_t shownUpdated;
uint8_t pageIndex, pageCount = 1;
uint32_t pageAt;                            // millis() when the current page went up

// The two notices that belong to this screen only.
enum LocalNotice : int8_t { L_OFF = 100, L_NONE = 101 };

void resetContent() {
  shownHeader = -1;
  shownHeaderSig = 0;
  memset(shownBlock, 0, sizeof(shownBlock));
  shownNet = 0;
  shownPaged = -1;
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

// How many blocks the answer holds: CPU, RAM, two per GPU (load, video memory), one per disk.
uint8_t totalBlocks(const portal::Summary &d) { return (uint8_t)(2 + 2 * d.resources.gpuN + d.resources.n); }

// "<name>   <tail>", the name giving way so that the whole label stays inside its padding.
void makeLabel(char *out, size_t cap, const char *name, const char *tail) {
  char fitted[24];
  portal_ui::fitText(fitted, sizeof(fitted), name, LABEL_W - tft.textWidth(tail, 2), 2);
  snprintf(out, cap, "%s%s", fitted, tail);
}

// Builds block `index` (0 CPU, 1 RAM, then GPUs, then disks). False when there is no such block.
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
  } else if (index < 2 + 2 * r.gpuN) {
    // A GPU is two blocks: its load, then its video memory. With several cards each carries its number.
    const portal::Gpu &g = r.gpus[(index - 2) / 2];
    char number[4] = "";
    if (r.gpuCount > 1) snprintf(number, sizeof(number), "%u", (unsigned)((index - 2) / 2 + 1));
    if ((index - 2) % 2 == 0) {
      char name[28], tail[12] = "";
      snprintf(name, sizeof(name), "GPU%s %s", number, g.name[0] ? g.name : "");
      if (g.tempC != NA) snprintf(tail, sizeof(tail), "   %dC", g.tempC);
      makeLabel(b.label, sizeof(b.label), name, tail);
      b.pct = g.pct;
      b.sev = g.sev;
    } else {
      char used[10], total[10];
      formatTenths(used, sizeof(used), g.memUsedDg);
      formatTenths(total, sizeof(total), g.memTotalDg);
      if (g.memTotalDg) snprintf(b.label, sizeof(b.label), "VRAM%s   %s / %s GB", number, used, total);
      else snprintf(b.label, sizeof(b.label), "VRAM%s", number);
      // The portal makes no judgement about video memory (a full one is what a busy card should have):
      // the bar is the plain colour, and only the percentage moves.
      b.pct = g.memTotalDg ? (int16_t)(((uint32_t)g.memUsedDg * 100 + g.memTotalDg / 2) / g.memTotalDg) : NA;
      if (b.pct != NA && b.pct > 100) b.pct = 100;
      b.sev = portal::SEV_OK;
    }
  } else {
    uint8_t disk = index - 2 - 2 * r.gpuN;
    if (disk >= r.n) return false;
    // "Media   460 GB free"
    char size[12], tail[24];
    formatSize(size, sizeof(size), r.disks[disk].freeGb);
    snprintf(tail, sizeof(tail), "   %s free", size);
    makeLabel(b.label, sizeof(b.label), r.disks[disk].name[0] ? r.disks[disk].name : "disk", tail);
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

// The page the blocks are on: the blocks are split evenly over as few pages as hold them (eight blocks
// are two pages of four, not six and two), and `turned` says the page just changed.
void updateContent(bool turned) {
  const portal::Summary &d = portal::data();
  const uint32_t updated = portal::updatedAtMs();
  if (updated == shownUpdated && shownHeader >= 0 && !turned) return;   // nothing new: nothing to do

  // The answer is what the header and the blocks are made of.
  uint32_t headerSig = highLoad(d) ? 1 : hashText(2, d.site);
  if (shownHeader < 0 || headerSig != shownHeaderSig) {
    drawHeader(d);
    shownHeader = highLoad(d) ? 1 : 0;
    shownHeaderSig = headerSig;
  }
  const uint8_t total = totalBlocks(d);
  pageCount = (uint8_t)((total + BLOCKS - 1) / BLOCKS);
  if (pageCount < 1) pageCount = 1;
  if (pageIndex >= pageCount) pageIndex = 0;
  const uint8_t perPage = (uint8_t)((total + pageCount - 1) / pageCount);
  for (uint8_t i = 0; i < BLOCKS; i++) {
    Block b;
    const uint8_t index = (uint8_t)(pageIndex * perPage + i);
    if (i < perPage && index < total && makeBlock(d, index, b)) {
      uint32_t sig = blockSignature(b);
      if (sig != shownBlock[i]) {
        drawBlock(i, b);
        shownBlock[i] = sig;
      }
    } else if (shownBlock[i]) {   // a disk that is gone, or a last page with fewer blocks
      tft.fillRect(0, BLOCK_Y + i * BLOCK_H, config::SCREEN_W, BLOCK_H, TFT_BLACK);
      shownBlock[i] = 0;
    }
  }

  // The footer: the network rates, and with several pages "Page 2/3" at the right.
  const bool paged = pageCount > 1;
  char net[40], page[12] = "";
  makeNetLine(d, net, sizeof(net));
  if (paged) snprintf(page, sizeof(page), "Page %u/%u", (unsigned)pageIndex + 1, (unsigned)pageCount);
  uint32_t netSig = hashText(hashText(3, net), page);
  if (netSig != shownNet || shownPaged != (paged ? 1 : 0)) {
    if (shownPaged != (paged ? 1 : 0) && shownPaged >= 0) {   // the other layout: what it drew is in the way
      tft.fillRect(0, NET_Y, config::SCREEN_W, 20, TFT_BLACK);
    }
    tft.setTextColor(portal_ui::GREY, TFT_BLACK);
    if (paged) {
      char fitted[40];
      portal_ui::fitText(fitted, sizeof(fitted), net, NET_PAGED_W, 2);
      tft.setTextDatum(TL_DATUM);
      tft.setTextPadding(NET_PAGED_W);
      tft.drawString(fitted, BAR_X, NET_Y, 2);
      tft.setTextDatum(TR_DATUM);
      tft.setTextPadding(PAGE_W);
      tft.drawString(page, config::SCREEN_W - BAR_X, NET_Y, 2);
    } else {
      tft.setTextDatum(TC_DATUM);
      tft.setTextPadding(config::SCREEN_W - 2);
      tft.drawString(net, config::SCREEN_W / 2, NET_Y, 2);
    }
    tft.setTextPadding(0);
    shownNet = netSig;
    shownPaged = paged ? 1 : 0;
  }
  shownUpdated = updated;
}

}  // namespace

void screenResourcesEnter() {
  resetShown();
  pageIndex = 0;
  pageAt = millis();
}

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
  bool turned = false;
  if (pageCount > 1 && millis() - pageAt >= (uint32_t)settings::get().portalPage * 1000UL) {
    pageIndex = (uint8_t)((pageIndex + 1) % pageCount);
    pageAt = millis();
    turned = true;
  }
  updateContent(turned);
}

void screenResourcesLeave() {}
