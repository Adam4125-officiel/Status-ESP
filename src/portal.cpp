// Status-Portal client. See portal.h for the contract (when it may block, what it caches).
#include "portal.h"

#include <stdarg.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <new>

#include "http_body.h"
#include "media.h"
#include "net.h"
#include "settings.h"
#include "weather.h"

namespace portal {

namespace {

const uint32_t HTTP_TIMEOUT_MS = 3000;        // connect, headers and each stalled read: a LAN is quick
const uint32_t RETRY_FIRST_MS = 30000;        // after the first failure, doubling, never more than RETRY_MAX_MS
const uint32_t RETRY_MAX_MS = 300000;
const uint32_t NO_MEMORY_RETRY_MS = 10000;    // heap too fragmented right now: look again soon
const uint32_t AFTER_WEATHER_MS = 2500;       // keep clear of a weather request that has just blocked the loop
const size_t MAX_BODY = 7168;                 // the contract's bound with services=all (Status-Portal >= 1.11.0)
const uint32_t MIN_FREE_HEAP = 15000;         // the body, the parse (~8 KB at its peak), HTTPClient and the socket
const uint32_t MIN_FREE_BLOCK = 6000;
const uint32_t MIN_STALE_MS = 180000;         // an answer is "fresh" for three intervals, three minutes at least

Summary cache;
bool haveData = false;
uint32_t updatedMs = 0;
Diag dg;
bool forceNow = false;        // fetch at the next pass, even inside the back-off
bool attempted = false;       // at least one attempt since boot / since the settings changed
bool lastOk = false;
bool lastWasMemorySkip = false;
uint32_t lastAttemptMs = 0;

void forgetData() {
  memset(&cache, 0, sizeof(cache));
  memset(&dg, 0, sizeof(dg));
  haveData = false;
  updatedMs = 0;
  attempted = false;
}

// Records why the portal is not answering (shown in the web interface and on the screens) and prints
// it. `fmt` is a PSTR() printf format.
void fail(PGM_P fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf_P(dg.error, sizeof(dg.error), fmt, args);
  va_end(args);
  Serial.printf_P(PSTR("[portal] %s\n"), dg.error);
}

// A reason that is not an attempt (no Wi-Fi): set quietly, only when it changes.
void notice(PGM_P msg) {
  if (strcmp_P(dg.error, msg) == 0) return;
  strncpy_P(dg.error, msg, sizeof(dg.error) - 1);
  dg.error[sizeof(dg.error) - 1] = '\0';
}

uint8_t requestedSections() { return settings::get().portalSections & SEC_ALL; }

// "services,incidents,..." for the ones that are on. The portal wants at least one known name (an
// empty list means "everything"), so with every switch off ask for the cheapest one and read none.
void appendSections(String &url, uint8_t sections) {
  static const char *const NAMES[] = {"services", "incidents", "maintenance", "resources", "announcements"};
  bool first = true;
  for (uint8_t i = 0; i < 5; i++) {
    if (!(sections & (1u << i))) continue;
    if (!first) url += ',';
    url += NAMES[i];
    first = false;
  }
  if (first) url += NAMES[0];
}

enum Outcome : uint8_t { OK, FAILED, NO_MEMORY };

Outcome fetchOnce() {
  const settings::Settings &s = settings::get();

  // The GIF decoder (~24 KB in one block) and the HTTP client + parse never coexist: close it first,
  // the screen that was playing it reopens it.
  if (media::gifIsOpen()) media::gifClose();
  if (ESP.getFreeHeap() < MIN_FREE_HEAP || ESP.getMaxFreeBlockSize() < MIN_FREE_BLOCK) {
    fail(PSTR("heap too low (%u B free)"), (unsigned)ESP.getFreeHeap());
    return NO_MEMORY;
  }

  const uint8_t sections = requestedSections();
  String url;
  url.reserve(120);
  url += s.portalUrl;
  url += F("/api/device/summary?sections=");
  appendSections(url, sections);
  url += F("&services=all");   // every service, OK ones included (older portals ignore it)

  // HTTPClient::begin(client, url) keeps a CLONE of `client` and connects that one: this local object
  // never gets a connection, everything is read through the HTTPClient itself (see weather.cpp).
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked encoding: the stream is the raw body
  http.setReuse(false);  // one request, then the connection is closed ("Connection: close")
  if (!http.begin(client, url)) {
    fail(PSTR("not a usable address"));
    return FAILED;
  }
  http.addHeader(F("X-Api-Key"), s.portalKey);   // the key travels in this header only, never in the URL

  // The status code first, and only then the body: a 404 or a 405 answers with a ~2.4 KB HTML page that
  // is neither read nor buffered, the connection is just dropped.
  int code = http.GET();
  dg.httpCode = (int16_t)code;
  if (code != HTTP_CODE_OK) {
    http.end();
    if (code == 401) fail(PSTR("bad key (HTTP 401)"));
    else if (code == 404) fail(PSTR("HTTP 404: endpoint off or portal too old"));
    else if (code == 400) fail(PSTR("HTTP 400: the portal refused the request"));
    else if (code >= 300 && code < 400) fail(PSTR("HTTP %d: redirect, use the direct http address"), code);
    else if (code > 0) fail(PSTR("HTTP %d"), code);
    else if (code == -1) fail(PSTR("connection failed"));
    else if (code == -11) fail(PSTR("no answer (timeout)"));
    else if (code == -7) fail(PSTR("not an HTTP server"));
    else fail(PSTR("network error %d"), code);
    return FAILED;
  }

  // The size is announced (the portal always sends Content-Length); one that is already too big is not
  // read at all, and the String is made the right size at once instead of growing (and moving) in 4 KB.
  int size = http.getSize();
  if (size > (int)MAX_BODY) {
    http.end();
    fail(PSTR("answer too large (%d B)"), size);
    return FAILED;
  }
  BodySink sink(MAX_BODY);
  sink.text.reserve(size > 0 ? (unsigned)size + 1 : 2048);
  http.writeToPrint(&sink);   // read whole by HTTPClient's own loop, see http_body.h
  http.end();
  const String &body = sink.text;
  if (sink.overflow) {
    fail(PSTR("answer too large"));
    return FAILED;
  }
  if (size > 0 && body.length() != (unsigned)size) {
    fail(PSTR("answer cut short (%u of %d B)"), (unsigned)body.length(), size);
    return FAILED;
  }

  // Parsed into a temporary: a bad answer must leave the previous good one alone.
  Summary *incoming = new (std::nothrow) Summary;
  if (!incoming) {
    fail(PSTR("heap too low (answer of %u B)"), (unsigned)body.length());
    return NO_MEMORY;
  }
  char error[sizeof(dg.error)];
  ParseResult result = parse(body, sections, *incoming, error, sizeof(error));
  if (result != PARSE_OK) {
    fail(PSTR("%s"), error);
    delete incoming;
    return FAILED;
  }
  memcpy(&cache, incoming, sizeof(cache));
  delete incoming;
  haveData = true;
  updatedMs = millis() ? millis() : 1;
  return OK;
}

uint32_t retryWaitMs(uint16_t failures) {
  uint32_t wait = RETRY_FIRST_MS;
  for (uint16_t i = 1; i < failures && wait < RETRY_MAX_MS; i++) wait *= 2;
  if (wait > RETRY_MAX_MS) wait = RETRY_MAX_MS;
  uint32_t interval = (uint32_t)settings::get().portalInterval * 1000UL;
  return wait < interval ? wait : interval;
}

}  // namespace

void begin() { forgetData(); }

bool configured() {
  const settings::Settings &s = settings::get();
  return s.portalUrl[0] && s.portalKey[0];
}

void loop() {
  if (!configured()) {
    if (haveData || attempted) forgetData();   // the address or the key was cleared
    return;
  }
  if (!net::isConnected() || WiFi.status() != WL_CONNECTED) {
    if (!dg.error[0]) notice(PSTR("no Wi-Fi"));
    return;
  }

  if (strcmp_P(dg.error, PSTR("no Wi-Fi")) == 0) dg.error[0] = '\0';   // it is back

  uint32_t now = millis();
  if (!forceNow && attempted) {
    uint32_t wait = lastOk ? (uint32_t)settings::get().portalInterval * 1000UL
                           : (lastWasMemorySkip ? NO_MEMORY_RETRY_MS : retryWaitMs(dg.failures));
    if (now - lastAttemptMs < wait) return;
  }
  // The weather request blocks for a moment too: not two of them in a row.
  uint32_t weatherAt = weather::diag().attemptMs;
  if (weatherAt && now - weatherAt < AFTER_WEATHER_MS) return;
  forceNow = false;

  Outcome outcome = fetchOnce();
  attempted = true;
  lastAttemptMs = millis();
  lastOk = outcome == OK;
  lastWasMemorySkip = outcome == NO_MEMORY;
  dg.attemptMs = lastAttemptMs ? lastAttemptMs : 1;
  if (lastOk) {
    dg.error[0] = '\0';
    dg.failures = 0;
  } else if (dg.failures < 0xFFFF) {
    dg.failures++;
  }
}

const Summary &data() { return cache; }

bool fresh() {
  if (!haveData) return false;
  uint32_t stale = (uint32_t)settings::get().portalInterval * 3000UL;
  if (stale < MIN_STALE_MS) stale = MIN_STALE_MS;
  return millis() - updatedMs < stale;
}

uint32_t updatedAtMs() { return haveData ? updatedMs : 0; }

const Diag &diag() { return dg; }

uint32_t nowEpoch() {
  if (!haveData || cache.now == 0) return 0;
  return cache.now + (millis() - updatedMs) / 1000UL;
}

AlertLevel alertLevel() {
  if (!fresh()) return LEVEL_NONE;
  if (cache.overall == ST_DOWN) return LEVEL_DOWN;
  if (cache.overall == ST_DEGRADED) return LEVEL_DEGRADED;
  return LEVEL_NONE;
}

void requestRefresh() { forceNow = true; }

void settingsChanged() {
  forgetData();
  forceNow = true;
}

}  // namespace portal
