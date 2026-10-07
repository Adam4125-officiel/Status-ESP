// Host test of the two Status-Portal screens (screen_portal.cpp, screen_resources.cpp) and of what they
// share (portal_ui.cpp), driven by the real parser on the contract's own example and on worst-case
// answers. Built and run by tools/test_host.sh, with the address and memory sanitizers on.
//
// TFT_eSPI.h next to this file is a recording stand-in with the real glyph widths of Fonts 2 and 4. What
// it can say: what was drawn, how many calls an update made (no change must mean none), and whether any
// text leaves the screen or is wider than the padding meant to cover the text it replaces. What it cannot
// say is how anything looks: set PORTAL_OPS_DIR=<dir> and every scenario also leaves a <name>.ops file
// (one JSON line per drawing call) that a script can turn into a picture.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <time.h>

#include "config.h"
#include "display.h"
#include "net.h"
#include "portal.h"
#include "portal_ui.h"
#include "settings.h"

using namespace portal;

// ---- What the screens link against ---------------------------------------------------------------

TFT_eSPI tft;

static uint32_t g_ms = 100000;   // START_MS, below
uint32_t millis() { return g_ms; }

static settings::Settings g_settings;
namespace settings {
Settings &get() { return g_settings; }
}  // namespace settings

static bool g_online = true;
namespace net {
State state() { return g_online ? CONNECTED : ACCESS_POINT; }
}  // namespace net

// The two helpers of display.cpp the screens use, copied: display.cpp itself needs the whole firmware.
namespace display {
void drawFit(const char *text, int16_t y, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  tft.drawString(text, 120, y, tft.textWidth(text, 4) <= 232 ? 4 : 2);
  tft.setTextPadding(0);
}
void drawMessage(const char *title, const char *detail, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  tft.drawString(title, 120, 96, tft.textWidth(title, 4) <= 232 ? 4 : 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(detail, 120, 132, 2);
  tft.setTextPadding(0);
}
}  // namespace display

// The portal client's cache, set by the test instead of by a request.
static Summary g_data;
static bool g_have = false, g_fresh = false, g_configured = true;
static uint32_t g_updated = 0;
static Diag g_diag;
namespace portal {
const Summary &data() { return g_data; }
bool configured() { return g_configured; }
bool fresh() { return g_have && g_fresh; }
uint32_t updatedAtMs() { return g_have ? g_updated : 0; }
const Diag &diag() { return g_diag; }
uint32_t nowEpoch() { return (!g_have || g_data.now == 0) ? 0 : g_data.now + (g_ms - g_updated) / 1000UL; }   // as portal.cpp
}  // namespace portal

// ---- Test plumbing ---------------------------------------------------------------------------------

static int failures = 0;
static int checks = 0;

// What the screen drew since the last resetStats(), for the message of a failed check.
static void dumpDrawn() {
  printf("   drawn (%ld calls):", tft.ops);
  for (size_t i = 0; i < tft.drawn.size() && i < 14; i++) printf(" [%s]", tft.drawn[i].c_str());
  printf("\n");
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    checks++;                                                                \
    if (!(cond)) {                                                           \
      failures++;                                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
      dumpDrawn();                                                           \
    }                                                                        \
  } while (0)

static bool drew(const char *s) {
  for (const std::string &d : tft.drawn)
    if (d == s) return true;
  return false;
}

static bool drewContaining(const char *s) {
  for (const std::string &d : tft.drawn)
    if (d.find(s) != std::string::npos) return true;
  return false;
}

// Reports what the stand-in flagged, naming the scenario, then clears it.
static void expectClean(const char *scenario) {
  checks++;
  if (tft.violations) {
    failures++;
    printf("FAIL %s: %d layout problem(s)\n", scenario, tft.violations);
    for (const std::string &p : tft.problems) printf("   %s\n", p.c_str());
  }
}

static const char *g_opsDir = nullptr;
static void beginFrame(const char *name) {
  if (tft.log) {
    fclose(tft.log);
    tft.log = nullptr;
  }
  if (!g_opsDir) return;
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ops", g_opsDir, name);
  tft.log = fopen(path, "w");
}

// What the portal's clock says when the test clock reads START_MS: 2026-10-06T12:00:30Z, thirty seconds after
// the contract's example, so that a few seconds of test time do not cross a minute (the ages and the
// countdowns are in whole minutes, and a minute change is a reason to redraw).
static const uint32_t START_MS = 100000, START_EPOCH = 1791288030u;

static void defaults() {
  memset(&g_settings, 0, sizeof(g_settings));
  g_settings.portalInterval = 60;
  g_settings.portalPage = 6;
  g_settings.portalSections = SEC_ALL;
  g_settings.portalAlert = settings::PORTAL_ALERT_INDICATOR;
  g_online = true;
  g_configured = true;
  g_have = g_fresh = false;
  memset(&g_data, 0, sizeof(g_data));
  memset(&g_diag, 0, sizeof(g_diag));
  g_ms = START_MS;
}

// Hands the parsed answer to the screens as a fresh one, the way portal.cpp does after a good request. The
// portal's "now" is rewritten to follow the test clock, as a real portal's would between two requests.
static void install(std::string json, uint8_t sections = SEC_ALL) {
  size_t at = json.find("\"now\":\"");
  if (at != std::string::npos) {
    time_t t = (time_t)(START_EPOCH + (g_ms - START_MS) / 1000UL);
    struct tm tm;
    gmtime_r(&t, &tm);
    char iso[24];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    json.replace(at + 7, 20, iso);
  }
  Summary s;
  char err[64];
  ParseResult r = parse(json, sections, s, err, sizeof(err));
  CHECK(r == PARSE_OK);
  g_data = s;
  g_have = g_fresh = true;
  g_updated = g_ms;
  g_diag.error[0] = '\0';
  g_settings.portalSections = sections;
}

typedef void (*EnterFn)();
typedef void (*UpdateFn)(bool);

// What display.cpp does when it switches to a theme, then on every pass.
static void show(EnterFn enter, UpdateFn update) {
  enter();
  tft.fillScreen(TFT_BLACK);
  update(true);
}

// ---- The contract's example and variations of it -------------------------------------------------

static const char EXAMPLE[] =
    "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home Server\",\"overall\":\"down\","
    "\"services\":{\"total\":5,\"operational\":2,\"slow\":1,\"degraded\":1,\"maintenance\":0,\"down\":1,"
    "\"items\":[{\"name\":\"Nextcloud\",\"status\":\"down\"},{\"name\":\"Sonarr\",\"status\":\"degraded\"},"
    "{\"name\":\"Radarr\",\"status\":\"slow\"}]},"
    "\"incidents\":{\"open\":1,\"items\":[{\"title\":\"Nextcloud is unreachable\",\"status\":\"investigating\","
    "\"since\":\"2026-10-06T11:40:12Z\",\"services\":\"Nextcloud\"}]},"
    "\"maintenance\":{\"active\":1,\"upcoming\":1,\"items\":[{\"title\":\"Router firmware\",\"state\":\"active\","
    "\"services\":\"Seerr\",\"starts\":\"2026-10-06T11:30:00Z\",\"ends\":\"2026-10-06T12:30:00Z\"},"
    "{\"title\":\"Disk replacement\",\"state\":\"upcoming\",\"services\":\"Jellyfin, Radarr\","
    "\"starts\":\"2026-10-08T22:00:00Z\",\"ends\":\"2026-10-09T01:00:00Z\"}]},"
    "\"resources\":{\"cpu\":23.4,\"cpu_sev\":\"ok\",\"cpu_temp_c\":54.0,\"mem\":61.2,\"mem_sev\":\"warn\","
    "\"mem_used_gb\":9.8,\"mem_total_gb\":16.0,\"net_up_mb_s\":0.12,\"net_down_mb_s\":1.4,\"disk_count\":2,"
    "\"disks\":[{\"name\":\"Media\",\"pct\":88.5,\"sev\":\"crit\",\"free_gb\":460.2},"
    "{\"name\":\"/\",\"pct\":41.0,\"sev\":\"ok\",\"free_gb\":280.4}]},"
    "\"announcements\":{\"count\":1,\"items\":[{\"title\":\"Movie night\",\"text\":\"Friday at 8pm, bring snacks.\","
    "\"type\":\"info\",\"pinned\":true}]}}";

// Replaces the first occurrence of `from` in the example.
static std::string variant(const char *from, const char *to) {
  std::string s = EXAMPLE;
  size_t at = s.find(from);
  if (at == std::string::npos) {
    printf("FAIL: the example has no \"%s\"\n", from);
    failures++;
    return s;
  }
  return s.replace(at, strlen(from), to);
}

// Everything is fine: no service to list, nothing open.
static const char CALM[] =
    "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home Server\",\"overall\":\"operational\","
    "\"services\":{\"total\":5,\"operational\":5,\"slow\":0,\"degraded\":0,\"maintenance\":0,\"down\":0,\"items\":[]},"
    "\"incidents\":{\"open\":0,\"items\":[]},\"maintenance\":{\"active\":0,\"upcoming\":0,\"items\":[]},"
    "\"resources\":{\"cpu\":5.0,\"cpu_sev\":\"ok\",\"cpu_temp_c\":null,\"mem\":30.0,\"mem_sev\":\"ok\","
    "\"mem_used_gb\":4.8,\"mem_total_gb\":16.0,\"net_up_mb_s\":null,\"net_down_mb_s\":null,\"disk_count\":1,"
    "\"disks\":[{\"name\":\"/\",\"pct\":20.0,\"sev\":\"ok\",\"free_gb\":800.0}]},"
    "\"announcements\":{\"count\":0,\"items\":[]}}";

static void testPortalExample() {
  defaults();
  beginFrame("portal_example");
  install(EXAMPLE);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("DOWN"));                         // the banner
  CHECK(drew("OK 2") && drew("SLOW 1") && drew("DEGR 1") && drew("DOWN 1"));   // the counts
  CHECK(!drewContaining("MAINT "));            // no service is in maintenance: no such count
  CHECK(drew("Nextcloud") && drew("Sonarr") && drew("Radarr"));
  CHECK(drew("DEGRADED") && drew("SLOW"));
  CHECK(drewContaining("Nextcloud is un"));    // the title is cut to the room the row has
  CHECK(drew("20m"));                          // open since 11:40:12, and the portal's clock says 12:00:30
  CHECK(drew("Router firmware") && drew("29m left"));
  CHECK(drew("Disk replacement") && drew("in 2d"));
  CHECK(drewContaining("Movie night"));        // the ticker
  CHECK(!drew("All clear") && !drew("No portal data"));
  expectClean("portal example");

  // Nothing changed: not one drawing call, the whole point of drawing only what differs.
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 0);
  g_ms += 1500;   // the ticker holds still for a few seconds before it starts
  screenPortalUpdate(false);
  CHECK(tft.ops == 0);

  // The usual new answer a few seconds later, same content: nothing is redrawn, ticker included.
  install(EXAMPLE);
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 0);

  // The scroll moves on after its hold, one step at a time.
  g_ms += 4000;
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 1 && drewContaining("ovie night") && !drewContaining("Movie night"));
  // A new answer in the middle of it must not send it back to the beginning.
  install(EXAMPLE);
  g_ms += 300;
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 1 && drewContaining("vie night") && !drewContaining("Movie night"));
  expectClean("portal ticker");
}

static void testPortalChange() {
  defaults();
  beginFrame("portal_change");
  install(EXAMPLE);
  show(screenPortalEnter, screenPortalUpdate);

  // Sonarr goes down: its row changes, no other row does, and the banner stays as it was.
  g_ms += 1000;
  install(variant("{\"name\":\"Sonarr\",\"status\":\"degraded\"}", "{\"name\":\"Sonarr\",\"status\":\"down\"}"));
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drew("Sonarr") && drew("DOWN"));   // only its tag says DOWN now: the banner and the counts did not change
  CHECK(!drew("Nextcloud") && !drew("Radarr") && !drew("Router firmware"));
  CHECK(tft.ops <= 4);   // one row: the glyph cell, the dot, the text, the tag
  expectClean("portal change");

  // Only the minute moved (same data): the ages are drawn again because they changed, the names are not.
  g_ms += 60000;
  install(variant("{\"name\":\"Sonarr\",\"status\":\"degraded\"}", "{\"name\":\"Sonarr\",\"status\":\"down\"}"));
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drew("21m") && drew("28m left"));
  CHECK(!drew("Sonarr") && !drew("Radarr") && !drew("DOWN"));
  expectClean("portal minute");
}

static void testPortalSwitches() {
  defaults();
  beginFrame("portal_services_only");
  install(EXAMPLE, SEC_SERVICES);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("Nextcloud") && drew("Sonarr"));
  CHECK(!drew("Nextcloud is unreachable") && !drew("Router firmware") && !drewContaining("Movie night"));
  expectClean("services only");

  defaults();
  beginFrame("portal_announcements_only");
  install(EXAMPLE, SEC_ANNOUNCEMENTS);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("DOWN"));   // the overall status is always there
  CHECK(!drew("Nextcloud") && !drew("Sonarr") && !drew("OK 2"));
  CHECK(drewContaining("Movie night"));
  expectClean("announcements only");

  defaults();
  beginFrame("portal_none");
  install(EXAMPLE, 0);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("DOWN") && !drew("Nextcloud") && !drew("All clear"));
  expectClean("every switch off");
}

static void testPortalCalm() {
  defaults();
  beginFrame("portal_calm");
  install(CALM);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("OPERATIONAL") && drew("OK 5"));
  CHECK(drew("All clear"));
  expectClean("calm");

  // Something goes wrong: the words come off the screen before the rows go on it.
  g_ms += 60000;
  install(EXAMPLE);
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drew("DOWN") && drew("Nextcloud"));
  expectClean("calm to down");

  // And back.
  g_ms += 60000;
  install(CALM);
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drew("OPERATIONAL") && drew("All clear"));
  expectClean("down to calm");

  // Maintenance and slow are statuses of their own, and neither is an error.
  defaults();
  install(variant("\"overall\":\"down\"", "\"overall\":\"maintenance\""));
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("MAINTENANCE"));
  defaults();
  install(variant("\"overall\":\"down\"", "\"overall\":\"slow\""));
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("SLOW"));
}

// ---- The notices ---------------------------------------------------------------------------------------

static void testNotices() {
  defaults();
  g_configured = false;
  beginFrame("notice_not_configured");
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("Portal not set up"));
  expectClean("not configured");
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 0);   // the notice is drawn once

  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("Portal not set up"));

  defaults();
  g_online = false;
  g_diag.attemptMs = 0;
  beginFrame("notice_no_network");
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("No network"));
  expectClean("no network");

  defaults();
  beginFrame("notice_loading");
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("Contacting portal"));
  expectClean("loading");

  // Unreachable: the reason is on the screen, whatever its length.
  defaults();
  g_diag.attemptMs = 5;
  strlcpy(g_diag.error, "HTTP 404: endpoint off or portal too old", sizeof(g_diag.error));
  beginFrame("notice_unreachable_404");
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("No portal data"));
  CHECK(drewContaining("HTTP 404"));
  expectClean("unreachable 404");
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.ops == 0);
  // The reason changes: the notice is drawn again, once.
  strlcpy(g_diag.error, "bad key (HTTP 401)", sizeof(g_diag.error));
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drewContaining("bad key"));
  CHECK(tft.fills == 1);

  defaults();
  g_diag.attemptMs = 5;
  memset(g_diag.error, 'W', sizeof(g_diag.error) - 1);   // the longest reason there can be, all wide letters
  beginFrame("notice_unreachable_long");
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  expectClean("unreachable, longest reason");

  // From a notice to the real thing: the notice's words must be gone.
  defaults();
  g_diag.attemptMs = 5;
  strlcpy(g_diag.error, "connection failed", sizeof(g_diag.error));
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  install(EXAMPLE);
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(tft.fills == 1);   // cleared first
  CHECK(drew("DOWN") && drew("Nextcloud"));
  expectClean("notice to content");

  // An answer that has gone stale is not shown as current.
  g_ms += 400000;
  g_fresh = false;
  g_diag.attemptMs = 6;
  strlcpy(g_diag.error, "no answer (timeout)", sizeof(g_diag.error));
  tft.resetStats();
  screenPortalUpdate(false);
  CHECK(drewContaining("timeout") && drew("No portal data"));
  expectClean("stale");
}

// ---- The widest things the contract allows ---------------------------------------------------------------------

static std::string repeat(char c, size_t n) { return std::string(n, c); }

// Every list full, every string at its cap, made of the widest letters: the layout must hold.
static std::string worst(const char *overall) {
  std::string j = std::string("{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"") + repeat('W', 24) + "\",\"overall\":\"" + overall + "\",";
  j += "\"services\":{\"total\":999,\"operational\":1,\"slow\":2,\"degraded\":3,\"maintenance\":4,\"down\":989,\"items\":[";
  for (int i = 0; i < 6; i++) j += std::string(i ? "," : "") + "{\"name\":\"" + repeat('W', 24) + "\",\"status\":\"down\"}";
  j += "]},\"incidents\":{\"open\":9,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + repeat('W', 40) + "\",\"status\":\"monitoring\",\"since\":\"2026-09-06T11:40:12Z\",\"services\":\"" + repeat('W', 32) + "\"}";
  j += "]},\"maintenance\":{\"active\":1,\"upcoming\":8,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + repeat('W', 32) + "\",\"state\":\"" + (i ? "upcoming" : "active") + "\",\"services\":\"" + repeat('W', 32) +
         "\",\"starts\":\"2026-10-08T22:00:00Z\",\"ends\":\"2026-10-09T01:00:00Z\"}";
  j += "]},\"resources\":{\"cpu\":100.0,\"cpu_sev\":\"crit\",\"cpu_temp_c\":105.5,\"mem\":99.9,\"mem_sev\":\"crit\","
       "\"mem_used_gb\":1023.9,\"mem_total_gb\":1024.0,\"net_up_mb_s\":1234.56,\"net_down_mb_s\":9876.54,\"disk_count\":26,\"disks\":[";
  for (int i = 0; i < 4; i++)
    j += std::string(i ? "," : "") + "{\"name\":\"" + repeat('W', 16) + "\",\"pct\":99.9,\"sev\":\"crit\",\"free_gb\":12345.6}";
  j += "]},\"announcements\":{\"count\":3,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + repeat('W', 32) + "\",\"text\":\"" + repeat('W', 80) + "\",\"type\":\"critical\",\"pinned\":true}";
  j += "]}}";
  return j;
}

static void testWorstCase() {
  const std::string big = worst("down");
  CHECK(big.size() <= 4096);

  defaults();
  beginFrame("portal_worst");
  install(big);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  expectClean("portal, worst case");
  for (int i = 0; i < 40; i++) {   // let the ticker go all the way round
    g_ms += 1000;
    tft.resetStats();
    screenPortalUpdate(false);
    expectClean("portal, worst case, ticker");
  }

  defaults();
  beginFrame("resources_worst");
  install(big);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  expectClean("resources, worst case");
  CHECK(drew("HIGH LOAD"));

  // The same answers with the widest digits and no ticker room: only the largest counts.
  defaults();
  install(worst("degraded"), SEC_SERVICES | SEC_INCIDENTS);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  expectClean("portal, services and incidents, worst case");

  defaults();
  install(worst("maintenance"), SEC_MAINTENANCE | SEC_ANNOUNCEMENTS);
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  expectClean("portal, maintenance and announcements, worst case");
}

// ---- Resources ----------------------------------------------------------------------------------------------

static void testResources() {
  defaults();
  beginFrame("resources_example");
  install(EXAMPLE);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("Home Server"));              // no critical reading: the site name, not an alarm
  CHECK(!drew("HIGH LOAD"));
  CHECK(drewContaining("CPU") && drew("23%"));
  CHECK(drewContaining("RAM") && drewContaining("9.8 / 16.0 GB") && drew("61%"));
  CHECK(drewContaining("Media") && drewContaining("460 GB free") && drew("89%"));   // 88.5 rounds to 89
  CHECK(drewContaining("280 GB free") && drew("41%"));
  CHECK(drewContaining("down 1.4 MB/s"));
  expectClean("resources example");

  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(tft.ops == 0);

  // One reading moves: its block is drawn again, the others are not.
  g_ms += 60000;
  install(variant("\"cpu\":23.4", "\"cpu\":31.0"));
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drew("31%") && !drew("61%") && !drew("89%"));
  expectClean("resources, cpu moved");

  // The portal's own judgement raises the alarm, and the alarm can be taken back.
  g_ms += 60000;
  install(variant("\"cpu\":23.4,\"cpu_sev\":\"ok\"", "\"cpu\":97.0,\"cpu_sev\":\"crit\""));
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drew("HIGH LOAD") && drew("97%"));
  g_ms += 60000;
  install(EXAMPLE);
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(!drew("HIGH LOAD") && drew("Home Server"));
  expectClean("resources, alarm and back");

  // A host that reports almost nothing.
  defaults();
  beginFrame("resources_calm");
  install(CALM);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("5%") && drew("30%") && drew("20%"));
  CHECK(!drewContaining("MB/s"));          // the rates are null right after the portal starts
  expectClean("resources, mostly unknown");

  // Unknown readings are dashes, not zeros.
  defaults();
  install(variant("\"cpu\":23.4", "\"cpu\":null"));
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("--%"));
  expectClean("resources, cpu unknown");

  // The switch is off, or the portal could not read its resources.
  defaults();
  beginFrame("resources_off");
  install(EXAMPLE, SEC_SERVICES);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("Resources are off"));
  expectClean("resources switched off");

  defaults();
  beginFrame("resources_null");
  std::string nulled = EXAMPLE;
  size_t from = nulled.find("\"resources\":{"), to = nulled.find(",\"announcements\"");
  nulled.replace(from, to - from, "\"resources\":null");
  install(nulled);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("No resource data"));
  expectClean("resources null");
}


// ---- Resources, paged ---------------------------------------------------------------------------------------

// A resources answer with `gpus` cards and `disks` disks (resources=all, portal >= 1.11.0-rc.2).
static std::string pagedAnswer(int gpus, int disks, const char *name = "RTX 3080", int diskNameLen = 0) {
  std::string j = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home Server\",\"overall\":\"operational\","
                  "\"resources\":{\"cpu\":23.4,\"cpu_sev\":\"ok\",\"cpu_temp_c\":54.0,\"mem\":61.2,\"mem_sev\":\"warn\","
                  "\"mem_used_gb\":9.8,\"mem_total_gb\":16.0,\"net_up_mb_s\":0.12,\"net_down_mb_s\":1.4,\"disk_count\":";
  j += std::to_string(disks) + ",\"disks\":[";
  for (int i = 0; i < disks; i++) {
    std::string n = diskNameLen ? repeat('W', diskNameLen) : "Disk" + std::to_string(i + 1);
    j += std::string(i ? "," : "") + "{\"name\":\"" + n + "\",\"pct\":" + std::to_string(90 - i * 5) + ".0,\"sev\":\"" +
         (i < 2 ? "crit" : "ok") + "\",\"free_gb\":" + std::to_string(100 + i) + ".0}";
  }
  j += "],\"gpu_count\":" + std::to_string(gpus) + ",\"gpus\":[";
  for (int i = 0; i < gpus; i++)
    j += std::string(i ? "," : "") + "{\"name\":\"" + name + "\",\"pct\":" + std::to_string(40 + i) +
         ",\"sev\":\"ok\",\"mem_used_gb\":4.2,\"mem_total_gb\":10.0,\"temp_c\":61}";
  j += "]},\"announcements\":{\"count\":0,\"items\":[]}}";
  return j;
}

static void testResourcesPaging() {
  // CPU, RAM, one GPU (two blocks) and six disks: ten blocks, two pages of five (CPU..Disk1, Disk2..Disk6).
  defaults();
  beginFrame("resources_page1");
  install(pagedAnswer(1, 6));
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drewContaining("CPU") && drewContaining("RAM") && drewContaining("GPU RTX 3080") && drewContaining("61C"));
  CHECK(drewContaining("VRAM   4.2 / 10.0 GB") && drew("42%"));   // 4.2 of 10.0 GB
  CHECK(drewContaining("Disk1") && !drewContaining("Disk2"));
  CHECK(drew("Page 1/2") && drewContaining("down 1.4 MB/s"));
  expectClean("resources page 1");

  // Nothing changed and the page is not due yet: not one drawing call.
  g_ms += 3000;
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(tft.ops == 0);
  // A new answer with the same content does not redraw anything either.
  install(pagedAnswer(1, 6));
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(tft.ops == 0);

  // portal_page seconds after it went up, the next page replaces the blocks that differ.
  g_ms += 3000;
  beginFrame("resources_page2");
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drew("Page 2/2") && drewContaining("Disk2") && drewContaining("Disk6") && !drewContaining("Disk1"));
  CHECK(!drew("Home Server") && !drewContaining("CPU"));   // the header and the blocks that did not change
  expectClean("resources page 2");

  // And round to the first again.
  g_ms += 6000;
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drew("Page 1/2") && drewContaining("CPU"));
  expectClean("resources back to page 1");

  // Six blocks fit on one page: no "Page n/m", the rates stay centred and nothing turns.
  defaults();
  beginFrame("resources_one_page");
  install(pagedAnswer(0, 4));
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(!drewContaining("Page ") && drewContaining("Disk4") && drewContaining("down 1.4 MB/s"));
  g_ms += 20000;
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(tft.ops == 0);
  expectClean("resources, one page");

  // The answer shrinks to one page while the second was showing: back to the first, footer re-laid out.
  defaults();
  install(pagedAnswer(1, 6));
  show(screenResourcesEnter, screenResourcesUpdate);
  g_ms += 6000;
  screenResourcesUpdate(false);
  CHECK(drewContaining("Disk6"));
  install(pagedAnswer(0, 2));
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drewContaining("CPU") && !drewContaining("Page ") && drewContaining("Disk2"));
  expectClean("resources, shrinks to one page");

  // A portal that sends no GPUs (older, or none installed): CPU, RAM and the disks, as before.
  defaults();
  install(EXAMPLE);
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(!drewContaining("GPU") && !drewContaining("VRAM") && !drewContaining("Page "));

  // Several cards each carry their number.
  defaults();
  install(pagedAnswer(2, 0));
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drewContaining("GPU1 RTX 3080") && drewContaining("VRAM1") && drewContaining("GPU2 RTX 3080") && drewContaining("VRAM2"));
  CHECK(drew("Page 1/1") == false);   // six blocks: one page
  expectClean("resources, two GPUs");

  // The most the contract allows: four cards and eight disks with the widest letters, three pages of six.
  defaults();
  beginFrame("resources_paged_worst");
  install(pagedAnswer(4, 8, repeat('W', 20).c_str(), 16));
  tft.resetStats();
  show(screenResourcesEnter, screenResourcesUpdate);
  CHECK(drew("Page 1/3"));
  expectClean("resources, worst case page 1");
  for (int page = 2; page <= 3; page++) {
    g_ms += 6000;
    tft.resetStats();
    screenResourcesUpdate(false);
    char want[12];
    snprintf(want, sizeof(want), "Page %d/3", page);
    CHECK(drew(want));
    expectClean("resources, worst case, later page");
  }
  g_ms += 6000;
  tft.resetStats();
  screenResourcesUpdate(false);
  CHECK(drew("Page 1/3"));
  expectClean("resources, worst case, round again");
}

// An answer with a section the firmware does not know, or fields it does not use, changes nothing.
static void testNewerPortal() {
  defaults();
  install(variant("\"announcements\"", "\"future\":{\"x\":[1,2,3]},\"announcements\""));
  tft.resetStats();
  show(screenPortalEnter, screenPortalUpdate);
  CHECK(drew("Nextcloud"));
  expectClean("newer portal");
}

int main() {
  g_opsDir = getenv("PORTAL_OPS_DIR");
  testPortalExample();
  testPortalChange();
  testPortalSwitches();
  testPortalCalm();
  testNotices();
  testWorstCase();
  testResources();
  testResourcesPaging();
  testNewerPortal();
  beginFrame("");
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
