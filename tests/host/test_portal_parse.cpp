// Host test of the Status-Portal answer parser (src/portal_parse.cpp) and of the UTF-8 fold it uses.
// Built and run by tools/test_host.sh, with the address and memory sanitizers on.
//
// The first document is the full example from the contract (Status-Portal's
// GET /api/device/summary, schema version 1); the large one is built here with every list and every
// string at its cap; the truncated ones are every prefix of the first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "ascii.h"
#include "portal_data.h"
#include "portal_url.h"

using namespace portal;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    checks++;                                                                \
    if (!(cond)) {                                                           \
      failures++;                                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                        \
  } while (0)

#define CHECK_STR(actual, expected)                                                              \
  do {                                                                                           \
    checks++;                                                                                    \
    if (strcmp((actual), (expected)) != 0) {                                                     \
      failures++;                                                                                \
      printf("FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (actual), (expected)); \
    }                                                                                            \
  } while (0)

// The contract's example, all five sections, 1168 bytes as sent.
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

static ParseResult run(const char *body, uint8_t sections, Summary &out, char *err, size_t cap) {
  return parse(Text(body), sections, out, err, cap);
}

static void testExample() {
  CHECK(strlen(EXAMPLE) == 1168);
  Summary s;
  char err[64];
  CHECK(run(EXAMPLE, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK_STR(err, "");

  CHECK(s.now == 1791288000u);
  CHECK_STR(s.site, "Home Server");
  CHECK(s.overall == ST_DOWN);

  CHECK(s.services.present);
  CHECK(s.services.total == 5 && s.services.operational == 2 && s.services.slow == 1);
  CHECK(s.services.degraded == 1 && s.services.maintenance == 0 && s.services.down == 1);
  CHECK(s.services.n == 3);
  CHECK_STR(s.services.items[0].name, "Nextcloud");
  CHECK(s.services.items[0].status == ST_DOWN);
  CHECK_STR(s.services.items[1].name, "Sonarr");
  CHECK(s.services.items[1].status == ST_DEGRADED);
  CHECK_STR(s.services.items[2].name, "Radarr");
  CHECK(s.services.items[2].status == ST_SLOW);

  CHECK(s.incidents.present && s.incidents.open == 1 && s.incidents.n == 1);
  CHECK_STR(s.incidents.items[0].title, "Nextcloud is unreachable");
  CHECK(s.incidents.items[0].status == INC_INVESTIGATING);
  CHECK(s.incidents.items[0].since == 1791286812u);
  CHECK_STR(s.incidents.items[0].services, "Nextcloud");

  CHECK(s.maintenance.present && s.maintenance.active == 1 && s.maintenance.upcoming == 1 && s.maintenance.n == 2);
  CHECK_STR(s.maintenance.items[0].title, "Router firmware");
  CHECK(s.maintenance.items[0].active);
  CHECK(s.maintenance.items[0].starts == 1791286200u && s.maintenance.items[0].ends == 1791289800u);
  CHECK_STR(s.maintenance.items[1].services, "Jellyfin, Radarr");
  CHECK(!s.maintenance.items[1].active);
  CHECK(s.maintenance.items[1].ends == 1791507600u);

  CHECK(s.resources.present);
  CHECK(s.resources.cpu == 23 && s.resources.cpuSev == SEV_OK && s.resources.cpuTempC == 54);
  CHECK(s.resources.mem == 61 && s.resources.memSev == SEV_WARN);
  CHECK(s.resources.memUsedDg == 98 && s.resources.memTotalDg == 160);
  CHECK(s.resources.netUp > 0.11f && s.resources.netUp < 0.13f);
  CHECK(s.resources.netDown > 1.39f && s.resources.netDown < 1.41f);
  CHECK(s.resources.diskCount == 2 && s.resources.n == 2);
  CHECK_STR(s.resources.disks[0].name, "Media");
  CHECK(s.resources.disks[0].pct == 89 && s.resources.disks[0].sev == SEV_CRIT && s.resources.disks[0].freeGb == 460);
  CHECK_STR(s.resources.disks[1].name, "/");
  CHECK(s.resources.disks[1].pct == 41 && s.resources.disks[1].sev == SEV_OK && s.resources.disks[1].freeGb == 280);

  CHECK(s.announcements.present && s.announcements.count == 1 && s.announcements.n == 1);
  CHECK_STR(s.announcements.items[0].title, "Movie night");
  CHECK_STR(s.announcements.items[0].text, "Friday at 8pm, bring snacks.");
  CHECK(s.announcements.items[0].type == ANN_INFO && s.announcements.items[0].pinned);
}

// A section that was not asked for is ignored even when the portal sends it, and the header is
// always read.
static void testSections() {
  Summary s;
  char err[64];
  CHECK(run(EXAMPLE, SEC_SERVICES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.present && s.services.n == 3);
  CHECK(!s.incidents.present && !s.maintenance.present && !s.resources.present && !s.announcements.present);
  CHECK(s.overall == ST_DOWN && s.now == 1791288000u);

  CHECK(run(EXAMPLE, 0, s, err, sizeof(err)) == PARSE_OK);   // header only
  CHECK(!s.services.present && s.overall == ST_DOWN);

  CHECK(run(EXAMPLE, SEC_RESOURCES | SEC_ANNOUNCEMENTS, s, err, sizeof(err)) == PARSE_OK);
  CHECK(!s.services.present && s.resources.present && s.announcements.present);

  // What the portal answers to "?sections=services": the header and that one object.
  const char *only =
      "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"X\",\"overall\":\"operational\","
      "\"services\":{\"total\":2,\"operational\":2,\"slow\":0,\"degraded\":0,\"maintenance\":0,\"down\":0,\"items\":[]}}";
  CHECK(run(only, SEC_SERVICES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.overall == ST_OPERATIONAL && s.services.present && s.services.total == 2 && s.services.n == 0);
  // Asked for more than the portal sent (an older portal): those sections are simply absent.
  CHECK(run(only, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.present && !s.incidents.present && !s.resources.present);
}

static void testEmptyAndNull() {
  Summary s;
  char err[64];
  const char *quiet =
      "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home\",\"overall\":\"maintenance\","
      "\"incidents\":{\"open\":0,\"items\":[]},\"maintenance\":{\"active\":0,\"upcoming\":0,\"items\":[]},"
      "\"resources\":null,\"announcements\":{\"count\":0,\"items\":[]}}";
  CHECK(run(quiet, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.overall == ST_MAINTENANCE);
  CHECK(s.incidents.present && s.incidents.open == 0 && s.incidents.n == 0);
  CHECK(s.maintenance.present && s.maintenance.n == 0);
  CHECK(!s.resources.present);   // null: the portal could not read its resources right now
  CHECK(s.announcements.present && s.announcements.n == 0);

  // Every number of the resources may be null.
  const char *nulls =
      "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home\",\"overall\":\"slow\","
      "\"resources\":{\"cpu\":null,\"cpu_sev\":\"ok\",\"cpu_temp_c\":null,\"mem\":null,\"mem_sev\":\"ok\","
      "\"mem_used_gb\":null,\"mem_total_gb\":null,\"net_up_mb_s\":null,\"net_down_mb_s\":null,\"disk_count\":0,"
      "\"disks\":[]}}";
  CHECK(run(nulls, SEC_RESOURCES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.overall == ST_SLOW);
  CHECK(s.resources.present && s.resources.cpu == NA && s.resources.cpuTempC == NA && s.resources.mem == NA);
  CHECK(s.resources.netUp < 0 && s.resources.netDown < 0 && s.resources.n == 0);

  // Extra keys and a newer minor shape are ignored.
  const char *extra =
      "{\"v\":1,\"future\":{\"a\":[1,2,3]},\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"Home\",\"overall\":\"degraded\","
      "\"services\":{\"total\":1,\"operational\":0,\"slow\":0,\"degraded\":1,\"maintenance\":0,\"down\":0,\"new\":7,"
      "\"items\":[{\"name\":\"A\",\"status\":\"degraded\",\"since\":\"x\"}]}}";
  CHECK(run(extra, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.overall == ST_DEGRADED && s.services.n == 1 && s.services.items[0].status == ST_DEGRADED);
}

static void testRefusals() {
  Summary s;
  char err[64];

  CHECK(run("{\"v\":2,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"down\"}", SEC_ALL, s, err, sizeof(err)) == PARSE_VERSION);
  CHECK(strstr(err, "v2") != nullptr);
  CHECK(run("{\"now\":\"2026-10-06T12:00:00Z\",\"overall\":\"down\"}", SEC_ALL, s, err, sizeof(err)) == PARSE_UNEXPECTED);
  CHECK(run("{\"v\":1,\"site\":\"H\"}", SEC_ALL, s, err, sizeof(err)) == PARSE_UNEXPECTED);
  CHECK(run("{\"v\":1,\"overall\":\"on fire\"}", SEC_ALL, s, err, sizeof(err)) == PARSE_UNEXPECTED);
  CHECK(run("{\"error\":\"Missing or invalid API key\"}", SEC_ALL, s, err, sizeof(err)) == PARSE_UNEXPECTED);
  CHECK(run("[1,2,3]", SEC_ALL, s, err, sizeof(err)) == PARSE_NOT_JSON);
  CHECK(run("", SEC_ALL, s, err, sizeof(err)) == PARSE_NOT_JSON);
  CHECK(strstr(err, "bad JSON") != nullptr);

  // The portal's 404 page: about 2.4 KB of HTML. (The client never reads it: it checks the status
  // code first. This is the belt to that pair of braces.)
  std::string html = "<!doctype html><html><head><title>404</title></head><body>";
  while (html.size() < 2400) html += "<p>The page you are looking for could not be found.</p>";
  html += "</body></html>";
  CHECK(run(html.c_str(), SEC_ALL, s, err, sizeof(err)) == PARSE_NOT_JSON);
  CHECK(s.services.present == false && s.overall == ST_UNKNOWN);

  // A tiny error buffer is never overrun and always terminated.
  char tiny[6];
  CHECK(run("{\"v\":9,\"overall\":\"down\"}", SEC_ALL, s, tiny, sizeof(tiny)) == PARSE_VERSION);
  CHECK(strlen(tiny) < sizeof(tiny));
  CHECK(parse(Text("{}"), SEC_ALL, s, nullptr, 0) == PARSE_UNEXPECTED);
}

// Every strict prefix of a valid document is a truncated answer: all of them must be refused,
// and none may crash the parser (this is what the sanitizers are for).
static void testTruncated() {
  Summary s;
  char err[64];
  size_t n = strlen(EXAMPLE);
  int accepted = 0;
  for (size_t cut = 0; cut < n; cut++) {
    if (parse(Text(EXAMPLE, cut), SEC_ALL, s, err, sizeof(err)) == PARSE_OK) accepted++;
  }
  CHECK(accepted == 0);
  CHECK(parse(Text(EXAMPLE, n - 1), SEC_ALL, s, err, sizeof(err)) == PARSE_NOT_JSON);
  CHECK(strstr(err, "IncompleteInput") != nullptr);
  CHECK(strstr(err, "1167 B") != nullptr);   // the length is in the message: it tells truncated from malformed
  CHECK(parse(Text(EXAMPLE, n), SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
}

// Appends `n` copies of `unit` (a UTF-8 string) cut to at most `bytes` bytes without splitting a character.
static std::string capped(const char *unit, size_t bytes) {
  std::string out;
  while (out.size() < bytes) {
    size_t before = out.size();
    for (const char *p = unit; *p && out.size() < bytes;) {
      size_t len = 1;
      unsigned char c = (unsigned char)*p;
      if (c >= 0xF0) len = 4;
      else if (c >= 0xE0) len = 3;
      else if (c >= 0xC0) len = 2;
      if (out.size() + len > bytes) return out;
      out.append(p, len);
      p += len;
    }
    if (out.size() == before) break;
  }
  return out;
}

// JSON string escaping for what the portal sends: quotes and backslashes only (it never sends \uXXXX).
static std::string esc(const std::string &raw) {
  std::string out;
  for (char c : raw) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

// The worst case the contract allows: every list full, every string at its cap (in decoded UTF-8
// bytes, as the portal counts them), made of the nastiest characters: a quote and a backslash (which
// double in size once escaped), accents, a ligature, an emoji.
static void testLargest() {
  const char *nasty = "\"A\\\xC3\xA9\xC3\x86\xF0\x9F\x98\x80z/";   // " A \ e-acute AE emoji z /
  std::string j = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"" + esc(capped("Caf\xC3\xA9 Server ", 24)) + "\",\"overall\":\"down\",";
  j += "\"services\":{\"total\":65535,\"operational\":1,\"slow\":2,\"degraded\":3,\"maintenance\":4,\"down\":5,\"items\":[";
  for (int i = 0; i < 6; i++) j += std::string(i ? "," : "") + "{\"name\":\"" + esc(capped(nasty, 24)) + "\",\"status\":\"down\"}";
  j += "]},\"incidents\":{\"open\":9,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 40)) + "\",\"status\":\"monitoring\",\"since\":\"2026-10-06T11:40:12Z\",\"services\":\"" +
         esc(capped(nasty, 32)) + "\"}";
  j += "]},\"maintenance\":{\"active\":1,\"upcoming\":2,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 32)) + "\",\"state\":\"upcoming\",\"services\":\"" + esc(capped(nasty, 32)) +
         "\",\"starts\":\"2026-10-08T22:00:00Z\",\"ends\":\"2026-10-09T01:00:00Z\"}";
  j += "]},\"resources\":{\"cpu\":100.0,\"cpu_sev\":\"crit\",\"cpu_temp_c\":105.5,\"mem\":99.9,\"mem_sev\":\"crit\","
       "\"mem_used_gb\":1023.9,\"mem_total_gb\":1024.0,\"net_up_mb_s\":1234.56,\"net_down_mb_s\":9876.54,\"disk_count\":26,\"disks\":[";
  for (int i = 0; i < 4; i++)
    j += std::string(i ? "," : "") + "{\"name\":\"" + esc(capped(nasty, 16)) + "\",\"pct\":99.9,\"sev\":\"crit\",\"free_gb\":12345.6}";
  j += "]},\"announcements\":{\"count\":3,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 32)) + "\",\"text\":\"" + esc(capped(nasty, 80)) + "\",\"type\":\"critical\",\"pinned\":true}";
  j += "]}}";

  printf("largest document: %zu bytes\n", j.size());
  CHECK(j.size() <= 4096);   // the contract's bound; the device caps the body at the same value

  Summary s;
  char err[64];
  CHECK(parse(j, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.n == 6 && s.incidents.n == 3 && s.maintenance.n == 3 && s.resources.n == 4 && s.announcements.n == 3);
  CHECK(s.services.total == 65535);
  CHECK(s.resources.mem == 100 && s.resources.cpu == 100 && s.resources.disks[0].pct == 100);
  CHECK(s.resources.diskCount == 26);
  CHECK(s.resources.memTotalDg == 10240);
  // Folded to printable ASCII: the quote, the backslash, the accent without its mark, the ligature
  // expanded, the emoji dropped.
  CHECK_STR(s.services.items[0].name, "\"A\\eAEz/\"A\\eAE");   // cut at 24 bytes before the second emoji
  for (size_t i = 0; i < s.announcements.n; i++) {
    for (const char *p = s.announcements.items[i].text; *p; p++) CHECK((unsigned char)*p >= 32 && (unsigned char)*p < 127);
    CHECK(strlen(s.announcements.items[i].text) <= MAX_ANN_TEXT);
  }
  CHECK_STR(s.site, "Cafe Server Cafe Serve");

  // Even with one more item than the contract allows the parser stops at the caps.
  std::string many = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"down\",\"services\":{\"total\":50,\"down\":50,\"items\":[";
  for (int i = 0; i < 50; i++) many += std::string(i ? "," : "") + "{\"name\":\"s" + std::to_string(i) + "\",\"status\":\"down\"}";
  many += "]}}";
  CHECK(parse(many, SEC_SERVICES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.n == MAX_SERVICE_ITEMS);
  CHECK_STR(s.services.items[5].name, "s5");

  // services=all (portal >= 1.11.0): operational services are listed too.
  std::string all = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"down\",\"services\":{\"total\":2,\"operational\":1,\"down\":1,\"items\":[{\"name\":\"Dead\",\"status\":\"down\"},{\"name\":\"Fine\",\"status\":\"operational\"}]}}";
  CHECK(parse(all, SEC_SERVICES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.n == 2);
  CHECK(s.services.items[1].status == ST_OPERATIONAL);
}

// resources=all (portal >= 1.11.0-rc.2): eight disks and up to four GPUs, with the nastiest strings.
static void testGpusAndTheLargestRequest() {
  const char *nasty = "\"A\\\xC3\xA9\xC3\x86\xF0\x9F\x98\x80z/";
  std::string j = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"down\",";
  j += "\"services\":{\"total\":65535,\"operational\":1,\"slow\":2,\"degraded\":3,\"maintenance\":4,\"down\":5,\"items\":[";
  for (int i = 0; i < 40; i++) j += std::string(i ? "," : "") + "{\"name\":\"" + esc(capped(nasty, 24)) + "\",\"status\":\"down\"}";
  j += "]},\"incidents\":{\"open\":9,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 40)) + "\",\"status\":\"monitoring\",\"since\":\"2026-10-06T11:40:12Z\",\"services\":\"" +
         esc(capped(nasty, 32)) + "\"}";
  j += "]},\"maintenance\":{\"active\":1,\"upcoming\":2,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 32)) + "\",\"state\":\"upcoming\",\"services\":\"" + esc(capped(nasty, 32)) +
         "\",\"starts\":\"2026-10-08T22:00:00Z\",\"ends\":\"2026-10-09T01:00:00Z\"}";
  j += "]},\"resources\":{\"cpu\":100.0,\"cpu_sev\":\"crit\",\"cpu_temp_c\":105.5,\"mem\":99.9,\"mem_sev\":\"crit\","
       "\"mem_used_gb\":1023.9,\"mem_total_gb\":1024.0,\"net_up_mb_s\":1234.56,\"net_down_mb_s\":9876.54,\"disk_count\":26,\"disks\":[";
  for (int i = 0; i < 8; i++)
    j += std::string(i ? "," : "") + "{\"name\":\"" + esc(capped(nasty, 16)) + "\",\"pct\":99.9,\"sev\":\"crit\",\"free_gb\":12345.6}";
  j += "],\"gpu_count\":9,\"gpus\":[";
  for (int i = 0; i < 4; i++)
    j += std::string(i ? "," : "") + "{\"name\":\"" + esc(capped(nasty, 20)) + "\",\"pct\":99.9,\"sev\":\"crit\",\"mem_used_gb\":9999.9,\"mem_total_gb\":9999.9,\"temp_c\":99.9}";
  j += "]},\"announcements\":{\"count\":3,\"items\":[";
  for (int i = 0; i < 3; i++)
    j += std::string(i ? "," : "") + "{\"title\":\"" + esc(capped(nasty, 32)) + "\",\"text\":\"" + esc(capped(nasty, 80)) + "\",\"type\":\"critical\",\"pinned\":true}";
  j += "]}}";
  printf("largest document with services=all and resources=all: %zu bytes\n", j.size());
  CHECK(j.size() <= 8192);   // the portal's ceiling for it (MAX_BYTES_ALL); the device caps the body at the same value

  Summary s;
  char err[64];
  CHECK(parse(j, SEC_ALL, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.services.n == MAX_SERVICE_ITEMS && s.resources.n == MAX_DISK_ITEMS && s.resources.gpuN == MAX_GPU_ITEMS);
  CHECK(s.resources.gpuCount == 9 && s.resources.diskCount == 26);
  CHECK(s.resources.gpus[3].pct == 100 && s.resources.gpus[3].sev == SEV_CRIT && s.resources.gpus[3].tempC == 100);
  CHECK(s.resources.gpus[0].memUsedDg == 65535);   // 9999.9 GB in tenths, clamped
  CHECK(strlen(s.resources.gpus[0].name) <= MAX_GPU_NAME);

  // More cards and disks than the contract allows: the parser stops at its caps.
  std::string many = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"operational\",\"resources\":{\"cpu\":1,\"disks\":[";
  for (int i = 0; i < 20; i++) many += std::string(i ? "," : "") + "{\"name\":\"d" + std::to_string(i) + "\",\"pct\":1}";
  many += "],\"gpus\":[";
  for (int i = 0; i < 20; i++) many += std::string(i ? "," : "") + "{\"name\":\"g" + std::to_string(i) + "\",\"pct\":1}";
  many += "]}}";
  CHECK(parse(many, SEC_RESOURCES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.resources.n == MAX_DISK_ITEMS && s.resources.gpuN == MAX_GPU_ITEMS);
  CHECK(s.resources.gpuCount == MAX_GPU_ITEMS);   // never fewer than the listed ones, when the count is missing
  CHECK_STR(s.resources.gpus[3].name, "g3");

  // An older portal, or a host with no GPU: no key, or an empty list, and no cards.
  std::string none = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"operational\",\"resources\":{\"cpu\":1,\"disks\":[]}}";
  CHECK(parse(none, SEC_RESOURCES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.resources.gpuN == 0 && s.resources.gpuCount == 0);
  std::string empty = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"operational\",\"resources\":{\"cpu\":1,\"disks\":[],\"gpu_count\":0,\"gpus\":[]}}";
  CHECK(parse(empty, SEC_RESOURCES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.resources.gpuN == 0);

  // A GPU with nothing known about it: dashes and zeros, never garbage.
  std::string blank = "{\"v\":1,\"now\":\"2026-10-06T12:00:00Z\",\"site\":\"H\",\"overall\":\"operational\",\"resources\":{\"gpus\":[{\"name\":null,\"pct\":null,\"temp_c\":null}]}}";
  CHECK(parse(blank, SEC_RESOURCES, s, err, sizeof(err)) == PARSE_OK);
  CHECK(s.resources.gpuN == 1 && s.resources.gpus[0].pct == NA && s.resources.gpus[0].tempC == NA && s.resources.gpus[0].name[0] == '\0');
}

static void testTimestamps() {
  CHECK(parseTimestamp("2026-10-06T12:00:00Z") == 1791288000u);
  CHECK(parseTimestamp("2000-02-29T00:00:00Z") == 951782400u);
  CHECK(parseTimestamp("2100-03-01T00:00:00Z") == 4107542400u);
  CHECK(parseTimestamp("1970-01-01T00:00:00Z") == 0);        // 0 is "unknown": the epoch itself is never a real moment here
  CHECK(parseTimestamp("") == 0);
  CHECK(parseTimestamp(nullptr) == 0);
  CHECK(parseTimestamp("2026-10-06T12:00:00") == 0);          // no Z
  CHECK(parseTimestamp("2026-10-06 12:00:00Z") == 0);
  CHECK(parseTimestamp("2026-13-06T12:00:00Z") == 0);
  CHECK(parseTimestamp("2026-10-32T12:00:00Z") == 0);
  CHECK(parseTimestamp("2026-10-06T24:00:00Z") == 0);
  CHECK(parseTimestamp("2026-10-06T12:00:00+0Z") == 0);
  CHECK(parseTimestamp("2026-10-06T12:00:00ZZ") == 0);
  CHECK(parseTimestamp("20x6-10-06T12:00:00Z") == 0);
}

static void testFold() {
  char out[32];
  ascii::fold("Caf\xC3\xA9 \xC3\x86on \xC5\x92uvre \xC3\x9F", out, sizeof(out));
  CHECK_STR(out, "Cafe AEon OEuvre ss");
  ascii::fold("a\xE2\x80\x93" "b \xE2\x80\x99" "c", out, sizeof(out));
  CHECK_STR(out, "a-b 'c");
  ascii::fold("x\xF0\x9F\x98\x80y\tz\n", out, sizeof(out));   // emoji dropped, control characters dropped
  CHECK_STR(out, "xyz");
  ascii::fold("\xD0\x9F\xD1\x80\xD0\xB8", out, sizeof(out));   // Cyrillic has no ASCII form
  CHECK_STR(out, "");
  ascii::fold("0123456789", out, 5);                           // never writes past cap
  CHECK_STR(out, "0123");
  ascii::fold("abc   ", out, sizeof(out));                     // trailing spaces trimmed
  CHECK_STR(out, "abc");
  ascii::fold("ab\xC3", out, sizeof(out));                     // a lone lead byte at the end
  CHECK_STR(out, "ab");
  ascii::fold("ab", out, 0);                                   // cap 0 writes nothing
}

// What a person may type as the portal address, and what is refused with a reason.
static void testUrl() {
  char out[MAX_URL + 1];
  auto ok = [&](const char *typed, const char *want) {
    memset(out, 'x', sizeof(out));
    CHECK(checkUrl(typed, out, sizeof(out)) == URL_OK);
    CHECK_STR(out, want);
  };
  ok("http://192.0.2.10:5000", "http://192.0.2.10:5000");
  ok("192.0.2.10:5000", "http://192.0.2.10:5000");               // no scheme: http
  ok("  http://192.0.2.10:5000/  ", "http://192.0.2.10:5000");   // spaces and a trailing slash
  ok("HTTP://NAS.lan:8080///", "http://NAS.lan:8080");
  ok("http://portal", "http://portal");                              // no port: 80
  ok("http://my_host-1.example.org:65535", "http://my_host-1.example.org:65535");

  auto refused = [&](const char *typed, UrlCheck why) {
    out[0] = 'x';
    out[1] = '\0';
    CHECK(checkUrl(typed, out, sizeof(out)) == why);
    CHECK_STR(out, "x");   // never written on a refusal
  };
  refused("https://192.0.2.10:5000", URL_HTTPS);
  refused("HTTPS://portal.example.org", URL_HTTPS);
  refused("ftp://192.0.2.10", URL_SCHEME);
  refused("ws://x:1", URL_SCHEME);
  refused("http://user:pass@192.0.2.10:5000", URL_CREDENTIALS);
  refused("http://192.0.2.10:5000/admin/device", URL_PATH);
  refused("http://192.0.2.10:5000?x=1", URL_PATH);
  refused("192.0.2.10:5000#top", URL_PATH);
  refused("http://", URL_HOST);
  refused("", URL_HOST);
  refused("   ", URL_HOST);
  refused(":5000", URL_HOST);
  refused("http://[::1]:5000", URL_HOST);
  refused("http://bad host:5000", URL_HOST);
  refused("http://caf\xC3\xA9:5000", URL_HOST);
  refused("http://192.0.2.10:", URL_PORT);
  refused("http://192.0.2.10:0", URL_PORT);
  refused("http://192.0.2.10:65536", URL_PORT);
  refused("http://192.0.2.10:123456", URL_PORT);
  refused("http://192.0.2.10:80a", URL_PORT);
  refused("http://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", URL_HOST);   // host over 40 characters
  // The longest address that fits is 40 host characters + a port; one more byte of room is refused.
  char tiny[10];
  CHECK(checkUrl("http://192.0.2.10:5000", tiny, sizeof(tiny)) == URL_TOO_LONG);

  CHECK(validKey("0123456789abcdef"));
  CHECK(validKey("a1b2c3d4"));
  CHECK(!validKey("short"));
  CHECK(!validKey("has a space inside"));
  CHECK(!validKey("tab\tinside-the-key"));
  CHECK(!validKey("caf\xC3\xA9-is-not-ascii"));
  CHECK(!validKey(""));
  CHECK(!validKey(nullptr));
  CHECK(validKey(std::string(MAX_KEY, 'k').c_str()));
  CHECK(!validKey(std::string(MAX_KEY + 1, 'k').c_str()));
}

// Random damage to a valid answer must never crash or overrun: every outcome is fine, so long as
// the sanitizers stay quiet and a "good" result still respects the caps.
static void testFuzz() {
  srand(12345);
  std::string base = EXAMPLE;
  Summary s;
  char err[64];
  int ok = 0, refused = 0;
  for (int iter = 0; iter < 20000; iter++) {
    std::string m = base;
    int edits = 1 + rand() % 6;
    for (int e = 0; e < edits; e++) {
      size_t at = (size_t)rand() % m.size();
      switch (rand() % 4) {
        case 0: m[at] = (char)(rand() & 0xFF); break;
        case 1: m.erase(at, 1 + rand() % 4); break;
        case 2: m.insert(at, 1, "{}[]\":,\\0123456789nul"[rand() % 21]); break;
        default: m[at] = (char)(0x80 + rand() % 0x80); break;
      }
      if (m.empty()) m = "x";
    }
    ParseResult r = parse(m, (uint8_t)(rand() & SEC_ALL), s, err, sizeof(err));
    if (r == PARSE_OK) {
      ok++;
      CHECK(s.services.n <= MAX_SERVICE_ITEMS && s.incidents.n <= MAX_INCIDENT_ITEMS);
      CHECK(strlen(s.site) <= MAX_SITE);
    } else {
      refused++;
    }
  }
  printf("fuzz: %d accepted, %d refused\n", ok, refused);
  CHECK(ok > 0 && refused > 0);
}

int main() {
  testExample();
  testSections();
  testEmptyAndNull();
  testRefusals();
  testTruncated();
  testLargest();
  testGpusAndTheLargestRequest();
  testTimestamps();
  testFold();
  testUrl();
  testFuzz();
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
