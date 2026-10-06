// The Status-Portal answer, parsed into plain fixed-size structs, and the parser.
//
// Contract: Status-Portal 1.10.0 or newer, GET /api/device/summary (schema version 1). The answer is
// compact JSON of at most 4096 bytes whose every list and string has a fixed cap; the structs below
// have room for exactly those caps, so nothing here allocates and a hostile or newer portal cannot
// make anything larger. All text is folded to printable ASCII on the way in (ascii.h).
//
// This file and portal_parse.cpp need nothing from the device (no Arduino, no network), so a PC test
// builds them (tools/test_host.sh).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#endif

namespace portal {

// What an answer is held in: the device's String (which the HTTP client fills), std::string on a PC.
// Handing the parser the same type the web server's JSON bodies use lets it share ArduinoJson's
// reader for it, which is about 1.5 KB of flash that a second reader type would cost.
#ifdef ARDUINO
typedef String Text;
#else
typedef std::string Text;
#endif

// Ordered by severity, so "worse" is simply ">". The portal's own precedence is
// down > degraded > maintenance > slow > operational.
enum Status : uint8_t {
  ST_UNKNOWN = 0,
  ST_OPERATIONAL,
  ST_SLOW,
  ST_MAINTENANCE,
  ST_DEGRADED,
  ST_DOWN
};

enum IncidentStatus : uint8_t { INC_UNKNOWN = 0, INC_INVESTIGATING, INC_IDENTIFIED, INC_MONITORING };
enum Severity : uint8_t { SEV_OK = 0, SEV_WARN, SEV_CRIT };
enum AnnouncementType : uint8_t { ANN_INFO = 0, ANN_WARNING, ANN_CRITICAL, ANN_SUCCESS };

// Sections of the answer, as a bit mask (the web interface's five switches, and the `sections=` query).
enum Section : uint8_t {
  SEC_SERVICES = 1,
  SEC_INCIDENTS = 2,
  SEC_MAINTENANCE = 4,
  SEC_RESOURCES = 8,
  SEC_ANNOUNCEMENTS = 16,
  SEC_ALL = 31
};

// The caps of the contract (UTF-8 bytes; a folded string is never longer).
const size_t MAX_SITE = 24;
const size_t MAX_SERVICE_ITEMS = 40, MAX_SERVICE_NAME = 24;
const size_t MAX_INCIDENT_ITEMS = 3, MAX_INCIDENT_TITLE = 40, MAX_AFFECTED = 32;
const size_t MAX_MAINT_ITEMS = 3, MAX_MAINT_TITLE = 32;
const size_t MAX_DISK_ITEMS = 4, MAX_DISK_NAME = 16;
const size_t MAX_ANN_ITEMS = 3, MAX_ANN_TITLE = 32, MAX_ANN_TEXT = 80;

const int16_t NA = -32768;   // "no value" for the integer readings below (the portal sends null)

struct Service {
  char name[MAX_SERVICE_NAME + 1];
  uint8_t status;               // ST_*; ST_OPERATIONAL only with services=all (portal >= 1.11.0)
};

struct Incident {
  char title[MAX_INCIDENT_TITLE + 1];
  char services[MAX_AFFECTED + 1];
  uint8_t status;               // INC_*
  uint32_t since;               // seconds since 1970 (UTC), 0 = unknown
};

struct Maintenance {
  char title[MAX_MAINT_TITLE + 1];
  char services[MAX_AFFECTED + 1];
  bool active;                  // false = upcoming
  uint32_t starts, ends;        // seconds since 1970 (UTC), 0 = unknown
};

struct Disk {
  char name[MAX_DISK_NAME + 1];
  int16_t pct;                  // used percent, rounded; NA when unknown
  uint8_t sev;                  // SEV_*
  uint16_t freeGb;              // rounded
};

struct Announcement {
  char title[MAX_ANN_TITLE + 1];
  char text[MAX_ANN_TEXT + 1];
  uint8_t type;                 // ANN_*
  bool pinned;
};

struct Summary {
  uint8_t sections;             // the sections this answer was read for (a Section mask)
  uint32_t now;                 // the portal's clock, seconds since 1970 (UTC), 0 = unknown
  char site[MAX_SITE + 1];
  uint8_t overall;              // ST_*

  struct {
    bool present;
    uint16_t total, operational, slow, degraded, maintenance, down;
    uint8_t n;
    Service items[MAX_SERVICE_ITEMS];
  } services;

  struct {
    bool present;
    uint16_t open;
    uint8_t n;
    Incident items[MAX_INCIDENT_ITEMS];
  } incidents;

  struct {
    bool present;
    uint16_t active, upcoming;
    uint8_t n;
    Maintenance items[MAX_MAINT_ITEMS];
  } maintenance;

  // present == false: the portal could not read its resources (the whole value is null), or the
  // section was not asked for.
  struct {
    bool present;
    int16_t cpu;                // percent, NA when unknown
    uint8_t cpuSev;
    int16_t cpuTempC;           // NA when unknown (most hosts expose nothing)
    int16_t mem;                // percent, NA when unknown
    uint8_t memSev;
    uint16_t memUsedDg, memTotalDg;   // gigabytes, in tenths
    float netUp, netDown;       // MB/s, negative when unknown
    uint8_t diskCount;          // how many disks the portal sees (the fullest ones are listed)
    uint8_t n;
    Disk disks[MAX_DISK_ITEMS];
  } resources;

  struct {
    bool present;
    uint16_t count;
    uint8_t n;
    Announcement items[MAX_ANN_ITEMS];
  } announcements;
};

enum ParseResult : uint8_t {
  PARSE_OK = 0,
  PARSE_NOT_JSON,      // empty, truncated or malformed: the message says which
  PARSE_UNEXPECTED,    // JSON, but not a Status-Portal summary
  PARSE_VERSION        // a schema version this firmware does not know
};

// Reads an answer into `out` (which is cleared first). `sections` is the mask the
// request asked for: only those parts are looked at, whatever else the portal sent. On failure
// `error` (up to errorCap bytes) holds a short reason for the diagnostics, "bad JSON: IncompleteInput
// (812 B)" for example. Allocates only the ArduinoJson document, and frees it before returning.
ParseResult parse(const Text &body, uint8_t sections, Summary &out, char *error, size_t errorCap);

// "2026-10-06T12:00:00Z" -> seconds since 1970 (UTC); 0 when it is not exactly that shape.
uint32_t parseTimestamp(const char *iso);

// "operational", "slow", "maintenance", "degraded" or "down" (the portal's own words); "unknown" otherwise.
inline const char *statusName(uint8_t s) {
  switch (s) {
    case ST_OPERATIONAL: return "operational";
    case ST_SLOW: return "slow";
    case ST_MAINTENANCE: return "maintenance";
    case ST_DEGRADED: return "degraded";
    case ST_DOWN: return "down";
    default: return "unknown";
  }
}

// Worse of two statuses.
inline uint8_t worse(uint8_t a, uint8_t b) { return a > b ? a : b; }

}  // namespace portal
