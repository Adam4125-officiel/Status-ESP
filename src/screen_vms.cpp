// Theme "vms" (Status-Portal): the Hyper-V virtual machines of the machine the portal runs on.
//
//   y   0..27   header: "Virtual machines". While Jellyfin is busy it is 32 px, split in two: that on top, a blue
//               band saying what Jellyfin is doing under it (the same header as the Resources screen)
//   y  32..     five rows of 34 px per page, each two lines: a dot in the colour of the state, the name, and
//               Hyper-V's own state word on the right (Running, Off, Paused...); under the name "up 3d 4h" while
//               it runs. With more VMs than fit they are split evenly over pages that turn every portal_page
//               seconds, as the Status-Portal and Resources screens' do
//   y 206..222  "3 of 5 running" (and "+2 more" when the portal sent fewer VMs than it has), with
//               "Page 2/3" on the right when there are several pages
//
// The data comes from the portal's `vms` section (portal 1.11.1 or newer, asked for with sections=...,vms), which
// the web interface's "Virtual machines" switch turns on and off. With no fresh answer the screen shows the
// notice of portal_ui.h; with the section off, or an older portal, or no VMs, it says so in its own words.
//
// Flicker: a row is redrawn only when its text or state changed, and the whole screen is never cleared to
// repaint it.
#include "config.h"
#include "display.h"
#include "portal.h"
#include "portal_ui.h"
#include "settings.h"

namespace {

const int16_t HEADER_H = 28;
const int16_t ROW_Y = 32, ROW_H = 34;
const int16_t DOT_X = 12, DOT_DY = 8, DOT_R = 5;
const int16_t NAME_X = 24, NAME_W = 128;           // the name, at the left
const int16_t STATE_RIGHT = 236, STATE_W = 84;     // the state word, right-aligned
const int16_t UP_DY = 17;                          // the uptime line, under the name
const int16_t FOOT_Y = 206;                        // the plastic over the glass hides the last ~14 px: see screen_hourly.cpp
const int16_t FOOT_PAGED_W = 160, PAGE_W = 70;
const uint8_t ROWS = 5;                            // rows on one page

int8_t shownNoticeKind;                            // portal_ui::Kind or the three local ones, -1 = content
uint32_t shownNoticeKey;
uint32_t shownHeader;                              // 0 = nothing
uint32_t shownRow[ROWS];                           // signature of what each row shows, 0 = nothing
uint32_t shownFoot;
int8_t shownPaged;                                 // 1 = the footer is laid out for several pages, 0 = one, -1 = nothing
uint32_t shownUpdated;
uint8_t pageIndex, pageCount = 1;
uint32_t pageAt;                                   // millis() when the current page went up

enum LocalNotice : int8_t { L_OFF = 100, L_OLD = 101, L_EMPTY = 102 };

void resetContent() {
  shownHeader = 0;
  memset(shownRow, 0, sizeof(shownRow));
  shownFoot = 0;
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

uint16_t kindColor(uint8_t kind) {
  switch (kind) {
    case portal::VM_RUNNING: return portal_ui::GREEN;
    case portal::VM_OFF: return portal_ui::GREY;
    case portal::VM_PAUSED: return portal_ui::YELLOW;
    case portal::VM_BUSY: return portal_ui::LIGHT_BLUE;
    default: return portal_ui::ORANGE;
  }
}

// --- Header -----------------------------------------------------------------------------------------------------

// 28 px: the title. While Jellyfin is busy it grows to 32 px and is split in two bands of 16 px, the title on
// top and a blue band with what Jellyfin is doing under it; the first row starts right below either way.
void drawHeader(const portal::Summary &d) {
  char jellyfin[48];
  portal_ui::jellyfinLine(d, jellyfin, sizeof(jellyfin));
  if (jellyfin[0]) {
    portal_ui::drawBand(0, 16, TFT_BLACK, portal_ui::LIGHT_GREY, "Virtual machines");
    portal_ui::drawBand(16, 16, portal_ui::BLUE, TFT_WHITE, jellyfin);
    return;
  }
  tft.fillRect(0, HEADER_H, config::SCREEN_W, ROW_Y - HEADER_H, TFT_BLACK);   // the band of the split header, if it was there
  tft.fillRect(0, 0, config::SCREEN_W, HEADER_H, TFT_BLACK);
  display::drawFit("Virtual machines", 1, portal_ui::LIGHT_GREY);
}

uint32_t headerSignature(const portal::Summary &d) {
  char jellyfin[48];
  portal_ui::jellyfinLine(d, jellyfin, sizeof(jellyfin));
  return hashText(2166136261u, jellyfin);
}

// --- Rows ---------------------------------------------------------------------------------------------------------

void drawRow(uint8_t index, const portal::Vm &v) {
  const int16_t y = ROW_Y + index * ROW_H;
  const uint16_t color = kindColor(v.kind);
  tft.fillRect(0, y, config::SCREEN_W, ROW_H, TFT_BLACK);
  tft.fillCircle(DOT_X, y + DOT_DY, DOT_R, color);

  char name[32];
  portal_ui::fitText(name, sizeof(name), v.name[0] ? v.name : "VM", NAME_W, 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(v.kind == portal::VM_OFF ? portal_ui::LIGHT_GREY : TFT_WHITE, TFT_BLACK);
  tft.drawString(name, NAME_X, y, 2);

  char state[16];
  portal_ui::fitText(state, sizeof(state), v.state[0] ? v.state : "?", STATE_W, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(state, STATE_RIGHT, y, 2);

  // The uptime only means something while the VM runs (Hyper-V reports 0 for the others).
  if (v.kind == portal::VM_RUNNING && v.up[0]) {
    char up[24];
    snprintf(up, sizeof(up), "up %s", v.up);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(portal_ui::GREY, TFT_BLACK);
    tft.drawString(up, NAME_X, y + UP_DY, 2);
  }
}

uint32_t rowSignature(const portal::Vm &v) {
  uint32_t h = hashText(2166136261u, v.name);
  h = hashText(h, v.state);
  h = hashText(h, v.up);
  return hashBytes(h, &v.kind, sizeof(v.kind));
}

// "3 of 5 running", or "3/12 running, +2 more" when the portal sent fewer VMs than it has (short enough for the
// room the page counter leaves it).
void makeFootLine(const portal::Summary &d, char *out, size_t cap) {
  const auto &v = d.vms;
  if (v.total > v.n) snprintf(out, cap, "%u/%u running, +%u more", (unsigned)v.running, (unsigned)v.total, (unsigned)(v.total - v.n));
  else snprintf(out, cap, "%u of %u running", (unsigned)v.running, (unsigned)v.total);
}

void drawNotice(const char *title, const char *detail) {
  tft.fillScreen(TFT_BLACK);
  display::drawMessage(title, detail, TFT_YELLOW);
}

// The rows are split evenly over as few pages as hold them (six VMs are two pages of three, not five and one),
// and `turned` says the page just changed.
void updateContent(bool turned) {
  const portal::Summary &d = portal::data();
  const uint32_t updated = portal::updatedAtMs();
  if (updated == shownUpdated && shownHeader && !turned) return;   // nothing new: nothing to do

  uint32_t headerSig = headerSignature(d);
  if (!shownHeader || headerSig != shownHeader) {
    drawHeader(d);
    shownHeader = headerSig;
  }

  const uint8_t total = d.vms.n;
  pageCount = (uint8_t)((total + ROWS - 1) / ROWS);
  if (pageCount < 1) pageCount = 1;
  if (pageIndex >= pageCount) pageIndex = 0;
  const uint8_t perPage = (uint8_t)((total + pageCount - 1) / pageCount);
  for (uint8_t i = 0; i < ROWS; i++) {
    const uint8_t index = (uint8_t)(pageIndex * perPage + i);
    if (i < perPage && index < total) {
      uint32_t sig = rowSignature(d.vms.items[index]);
      if (sig != shownRow[i]) {
        drawRow(i, d.vms.items[index]);
        shownRow[i] = sig;
      }
    } else if (shownRow[i]) {   // a VM that is gone, or a last page with fewer rows
      tft.fillRect(0, ROW_Y + i * ROW_H, config::SCREEN_W, ROW_H, TFT_BLACK);
      shownRow[i] = 0;
    }
  }

  const bool paged = pageCount > 1;
  char foot[48], page[12] = "";
  makeFootLine(d, foot, sizeof(foot));
  if (paged) snprintf(page, sizeof(page), "Page %u/%u", (unsigned)pageIndex + 1, (unsigned)pageCount);
  uint32_t footSig = hashText(hashText(3, foot), page);
  if (footSig != shownFoot || shownPaged != (paged ? 1 : 0)) {
    if (shownPaged != (paged ? 1 : 0) && shownPaged >= 0) {   // the other layout: what it drew is in the way
      tft.fillRect(0, FOOT_Y, config::SCREEN_W, 20, TFT_BLACK);
    }
    tft.setTextColor(portal_ui::GREY, TFT_BLACK);
    if (paged) {
      char fitted[48];
      portal_ui::fitText(fitted, sizeof(fitted), foot, FOOT_PAGED_W, 2);
      tft.setTextDatum(TL_DATUM);
      tft.setTextPadding(FOOT_PAGED_W);
      tft.drawString(fitted, 4, FOOT_Y, 2);
      tft.setTextDatum(TR_DATUM);
      tft.setTextPadding(PAGE_W);
      tft.drawString(page, config::SCREEN_W - 4, FOOT_Y, 2);
    } else {
      tft.setTextDatum(TC_DATUM);
      tft.setTextPadding(config::SCREEN_W - 2);
      tft.drawString(foot, config::SCREEN_W / 2, FOOT_Y, 2);
    }
    tft.setTextPadding(0);
    shownFoot = footSig;
    shownPaged = paged ? 1 : 0;
  }
  shownUpdated = updated;
}

}  // namespace

void screenVmsEnter() {
  resetShown();
  pageIndex = 0;
  pageAt = millis();
}

void screenVmsUpdate(bool full) {
  if (full) resetShown();

  // Why there is nothing to show, most general reason first: the portal's own states, then the three
  // that only this screen has.
  int8_t kind = (int8_t)portal_ui::notice();
  uint32_t key = 0;
  const char *title = nullptr, *detail = nullptr;
  if (kind != portal_ui::NONE) {
    key = portal_ui::noticeKey((portal_ui::Kind)kind);
  } else if (!(settings::get().portalSections & portal::SEC_VMS)) {
    kind = L_OFF;
    title = "VMs are off";
    detail = "Tick them in the web UI";
  } else if (!portal::data().vms.present) {
    kind = L_OLD;
    title = "No VM data";
    detail = "Needs Status-Portal 1.11.1";
  } else if (portal::data().vms.n == 0) {
    kind = L_EMPTY;
    title = "No virtual machines";
    detail = "The portal sees no Hyper-V VMs";
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

void screenVmsLeave() {}
