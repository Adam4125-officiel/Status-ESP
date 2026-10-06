// Parses the Status-Portal device summary (see portal_data.h for the contract and the caps).
//
// The answer is read whole into memory by the caller and parsed through an ArduinoJson filter built
// from the sections that were asked for, so a newer portal's extra keys, or a section the screens do
// not use, never take memory. Every number and string is read defensively: a missing key is "not
// there", a null or a value of the wrong type is "unavailable", and nothing a portal sends can write
// past a fixed buffer.
#include "portal_data.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

#include "ascii.h"

namespace portal {

namespace {

// Every ArduinoJson member lookup and conversion is a few hundred bytes of template code, and -Os
// would copy it to each of the ~70 places below. These helpers are deliberately not inlined: the
// firmware has little flash to spare (CLAUDE.md, rule 4), and this is the one place that reads a lot
// of fields.
#define NOINLINE __attribute__((noinline))

NOINLINE JsonVariantConst member(JsonVariantConst o, const char *key) { return o[key]; }

NOINLINE const char *text(JsonVariantConst o, const char *key) {
  JsonVariantConst v = member(o, key);
  return v.is<const char *>() ? v.as<const char *>() : "";
}

// A number, or false when the key is missing, null or not a number.
NOINLINE bool number(JsonVariantConst o, const char *key, float &out) {
  JsonVariantConst v = member(o, key);
  if (!v.is<float>()) return false;
  out = v.as<float>();
  return !isnan(out);
}

NOINLINE bool flag(JsonVariantConst o, const char *key) { return member(o, key).as<bool>(); }

NOINLINE JsonArrayConst items(JsonVariantConst o) { return member(o, "items").as<JsonArrayConst>(); }

NOINLINE void copyText(JsonVariantConst o, const char *key, char *dst, size_t cap) { ascii::fold(text(o, key), dst, cap); }

NOINLINE uint16_t count(JsonVariantConst o, const char *key) {
  float f;
  if (!number(o, key, f) || f < 0) return 0;
  return (uint16_t)(f > 65535.0f ? 65535 : f);
}

// A reading that may be null: rounded to a whole number, NA when it is not a number.
NOINLINE int16_t whole(JsonVariantConst o, const char *key) {
  float f;
  if (!number(o, key, f) || f < -1000.0f || f > 32000.0f) return NA;
  return (int16_t)lroundf(f);
}

NOINLINE int16_t percent(JsonVariantConst o, const char *key) {
  int16_t p = whole(o, key);
  return p == NA ? NA : (p < 0 ? 0 : (p > 100 ? 100 : p));
}

NOINLINE uint16_t tenths(JsonVariantConst o, const char *key) {
  float f;
  if (!number(o, key, f) || f < 0) return 0;
  f *= 10.0f;
  return (uint16_t)(f > 65535.0f ? 65535 : lroundf(f));
}

NOINLINE float rate(JsonVariantConst o, const char *key) {
  float f;
  return (number(o, key, f) && f >= 0) ? f : -1.0f;
}

// Index of `word` in a list of words (1-based, in the order given), 0 when it is not one of them.
NOINLINE uint8_t oneOf(const char *word, const char *const *words, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    if (strcmp(word, words[i]) == 0) return (uint8_t)(i + 1);
  }
  return 0;
}

// Same order as the Status enum: ST_OPERATIONAL, ST_SLOW, ST_MAINTENANCE, ST_DEGRADED, ST_DOWN.
const char *const STATUS_WORDS[] = {"operational", "slow", "maintenance", "degraded", "down"};
const char *const INCIDENT_WORDS[] = {"investigating", "identified", "monitoring"};
const char *const SEVERITY_WORDS[] = {"ok", "warn", "crit"};
const char *const ANNOUNCEMENT_WORDS[] = {"info", "warning", "critical", "success"};

NOINLINE uint8_t statusOf(JsonVariantConst o, const char *key) { return oneOf(text(o, key), STATUS_WORDS, 5); }
NOINLINE uint8_t severity(JsonVariantConst o, const char *key) {
  uint8_t i = oneOf(text(o, key), SEVERITY_WORDS, 3);
  return i ? (uint8_t)(i - 1) : (uint8_t)SEV_OK;
}

void readServices(JsonVariantConst o, Summary &out) {
  out.services.present = true;
  out.services.total = count(o, "total");
  out.services.operational = count(o, "operational");
  out.services.slow = count(o, "slow");
  out.services.degraded = count(o, "degraded");
  out.services.maintenance = count(o, "maintenance");
  out.services.down = count(o, "down");
  for (JsonVariantConst it : items(o)) {
    if (out.services.n >= MAX_SERVICE_ITEMS) break;
    Service &s = out.services.items[out.services.n];
    copyText(it, "name", s.name, sizeof(s.name));
    s.status = statusOf(it, "status");
    if (s.status == ST_UNKNOWN) continue;   // operational ones come with services=all (portal >= 1.11.0)
    out.services.n++;
  }
}

void readIncidents(JsonVariantConst o, Summary &out) {
  out.incidents.present = true;
  out.incidents.open = count(o, "open");
  for (JsonVariantConst it : items(o)) {
    if (out.incidents.n >= MAX_INCIDENT_ITEMS) break;
    Incident &i = out.incidents.items[out.incidents.n++];
    copyText(it, "title", i.title, sizeof(i.title));
    copyText(it, "services", i.services, sizeof(i.services));
    i.status = oneOf(text(it, "status"), INCIDENT_WORDS, 3);
    i.since = parseTimestamp(text(it, "since"));
  }
}

void readMaintenance(JsonVariantConst o, Summary &out) {
  out.maintenance.present = true;
  out.maintenance.active = count(o, "active");
  out.maintenance.upcoming = count(o, "upcoming");
  for (JsonVariantConst it : items(o)) {
    if (out.maintenance.n >= MAX_MAINT_ITEMS) break;
    Maintenance &m = out.maintenance.items[out.maintenance.n++];
    copyText(it, "title", m.title, sizeof(m.title));
    copyText(it, "services", m.services, sizeof(m.services));
    m.active = strcmp(text(it, "state"), "active") == 0;
    m.starts = parseTimestamp(text(it, "starts"));
    m.ends = parseTimestamp(text(it, "ends"));
  }
}

void readResources(JsonVariantConst o, Summary &out) {
  out.resources.present = true;
  out.resources.cpu = percent(o, "cpu");
  out.resources.cpuSev = severity(o, "cpu_sev");
  out.resources.cpuTempC = whole(o, "cpu_temp_c");
  out.resources.mem = percent(o, "mem");
  out.resources.memSev = severity(o, "mem_sev");
  out.resources.memUsedDg = tenths(o, "mem_used_gb");
  out.resources.memTotalDg = tenths(o, "mem_total_gb");
  out.resources.netUp = rate(o, "net_up_mb_s");
  out.resources.netDown = rate(o, "net_down_mb_s");
  uint16_t dc = count(o, "disk_count");
  out.resources.diskCount = (uint8_t)(dc > 255 ? 255 : dc);
  for (JsonVariantConst it : member(o, "disks").as<JsonArrayConst>()) {
    if (out.resources.n >= MAX_DISK_ITEMS) break;
    Disk &d = out.resources.disks[out.resources.n++];
    copyText(it, "name", d.name, sizeof(d.name));
    d.pct = percent(it, "pct");
    d.sev = severity(it, "sev");
    d.freeGb = (uint16_t)((tenths(it, "free_gb") + 5) / 10);
  }
  if (out.resources.diskCount < out.resources.n) out.resources.diskCount = out.resources.n;
}

void readAnnouncements(JsonVariantConst o, Summary &out) {
  out.announcements.present = true;
  out.announcements.count = count(o, "count");
  for (JsonVariantConst it : items(o)) {
    if (out.announcements.n >= MAX_ANN_ITEMS) break;
    Announcement &a = out.announcements.items[out.announcements.n++];
    copyText(it, "title", a.title, sizeof(a.title));
    copyText(it, "text", a.text, sizeof(a.text));
    uint8_t type = oneOf(text(it, "type"), ANNOUNCEMENT_WORDS, 4);
    a.type = type ? (uint8_t)(type - 1) : (uint8_t)ANN_INFO;
    a.pinned = flag(it, "pinned");
  }
}

void setError(char *error, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void setError(char *error, size_t cap, const char *fmt, ...) {
  if (!error || cap == 0) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(error, cap, fmt, args);
  va_end(args);
}

// A section's object, when it was asked for and the portal sent one (null and missing are both "no").
NOINLINE bool section(JsonVariantConst doc, uint8_t sections, uint8_t bit, const char *key, JsonVariantConst &obj) {
  obj = member(doc, key);
  return (sections & bit) && obj.is<JsonObjectConst>();
}

}  // namespace

// Seconds since 1970 of a UTC date and time, in 32-bit arithmetic only (the last second this can hold is
// in 2106, and the 64-bit multiplications would pull a libgcc routine into the firmware for nothing).
uint32_t parseTimestamp(const char *iso) {
  if (!iso || strlen(iso) != 20) return 0;
  static const char SHAPE[] = "dddd-dd-ddTdd:dd:ddZ";
  for (int i = 0; i < 20; i++) {
    char c = iso[i];
    if (SHAPE[i] == 'd' ? (c < '0' || c > '9') : c != SHAPE[i]) return 0;
  }
  auto two = [&](int at) { return (uint32_t)((iso[at] - '0') * 10 + (iso[at + 1] - '0')); };
  uint32_t year = (uint32_t)((iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0'));
  uint32_t month = two(5), day = two(8), hour = two(11), minute = two(14), second = two(17);
  if (year < 1971 || year > 2105 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 60)
    return 0;
  // Days since 1970-01-01 (Howard Hinnant's days_from_civil, with March as the first month of the year).
  uint32_t y = month <= 2 ? year - 1 : year;
  uint32_t era = y / 400;
  uint32_t yoe = y - era * 400;
  uint32_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  uint32_t days = era * 146097 + doe - 719468;
  uint32_t secs = days * 86400u + hour * 3600 + minute * 60 + second;
  return secs;
}

ParseResult parse(const Text &body, uint8_t sections, Summary &out, char *error, size_t errorCap) {
  memset(&out, 0, sizeof(out));
  out.sections = sections;
  if (error && errorCap) error[0] = '\0';

  // No filter: the answer is at most 4 KB, every list and string in it has a cap, and the sections that
  // were not asked for are not sent. (A filter would save little memory and cost 1.5 KB of flash.)
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    // The length tells a truncated answer from a malformed one: the whole thing is at most 4 KB.
    setError(error, errorCap, "bad JSON: %s (%u B)", err.c_str(), (unsigned)body.length());
    return PARSE_NOT_JSON;
  }
  if (!doc.is<JsonObject>()) {
    setError(error, errorCap, "answer is not a JSON object");
    return PARSE_NOT_JSON;
  }

  JsonVariantConst root = doc.as<JsonVariantConst>();
  JsonVariantConst version = member(root, "v");
  if (!version.is<int>()) {
    setError(error, errorCap, "not a Status-Portal summary");
    return PARSE_UNEXPECTED;
  }
  if (version.as<int>() != 1) {
    setError(error, errorCap, "portal API v%d unknown (update the firmware)", version.as<int>());
    return PARSE_VERSION;
  }
  out.overall = statusOf(root, "overall");
  if (out.overall == ST_UNKNOWN) {
    setError(error, errorCap, "unexpected answer: no overall status");
    return PARSE_UNEXPECTED;
  }
  out.now = parseTimestamp(text(root, "now"));
  copyText(root, "site", out.site, sizeof(out.site));

  JsonVariantConst obj;
  if (section(root, sections, SEC_SERVICES, "services", obj)) readServices(obj, out);
  if (section(root, sections, SEC_INCIDENTS, "incidents", obj)) readIncidents(obj, out);
  if (section(root, sections, SEC_MAINTENANCE, "maintenance", obj)) readMaintenance(obj, out);
  if (section(root, sections, SEC_RESOURCES, "resources", obj)) readResources(obj, out);
  if (section(root, sections, SEC_ANNOUNCEMENTS, "announcements", obj)) readAnnouncements(obj, out);
  return PARSE_OK;
}

}  // namespace portal
