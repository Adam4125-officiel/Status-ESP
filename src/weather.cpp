// Weather: Open-Meteo over plain HTTP, parsed as a stream through an ArduinoJson filter.
// See weather.h for the contract (when it may block, what it caches, the back-off).
#include "weather.h"

#include <stdarg.h>

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "http_body.h"
#include "media.h"
#include "net.h"
#include "settings.h"
#include "units.h"

namespace weather {

namespace {

const uint32_t HTTP_TIMEOUT_MS = 5000;     // connect, headers and each stalled read
const uint32_t FAILURE_BACKOFF_MS = 60000; // after a failed fetch
const uint32_t NO_MEMORY_RETRY_MS = 15000; // heap too fragmented right now: look again soon
const size_t MAX_BODY = 4096;              // the real answer is ~1.1 KB
const uint32_t MIN_FREE_BLOCK = 9000;      // HTTP client + filtered document, with margin

Data cache;
Diag dg;
bool forceNow = false;        // fetch at the next pass, even inside the back-off
bool attempted = false;       // at least one attempt since boot / since the location changed
bool lastOk = false;
bool lastWasMemorySkip = false;
uint32_t lastAttemptMs = 0;

// The location the cached data belongs to.
bool haveLocation = false;
float locLat = 0, locLon = 0;

bool sameLocation() {
  const settings::Settings &s = settings::get();
  return haveLocation && s.lat == locLat && s.lon == locLon;
}

void forgetData() {
  memset(&cache, 0, sizeof(cache));
  memset(&dg, 0, sizeof(dg));
  haveLocation = false;
  attempted = false;
}

// Records why the weather is not coming (shown in the web interface and on the empty weather
// screens) and prints it. `fmt` is a PSTR() printf format.
void fail(PGM_P fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf_P(dg.error, sizeof(dg.error), fmt, args);
  va_end(args);
  Serial.printf_P(PSTR("[weather] %s\n"), dg.error);
}

// A reason that is not an attempt (no city, no Wi-Fi): set quietly, only when it changes.
void notice(PGM_P msg) {
  if (strcmp_P(dg.error, msg) == 0) return;
  strncpy_P(dg.error, msg, sizeof(dg.error) - 1);   // zero-padded, and the last byte is already 0
  dg.error[sizeof(dg.error) - 1] = '\0';
}

// Day of the week (0 = Sunday) of an ISO date "YYYY-MM-DD", -1 if it is not one.
int8_t weekdayOf(const char *iso) {
  if (!iso || strlen(iso) < 10 || iso[4] != '-' || iso[7] != '-') return -1;
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  if (y < 2000 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  static const uint8_t OFFSET[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y--;
  return (int8_t)((y + y / 4 - y / 100 + y / 400 + OFFSET[m - 1] + d) % 7);
}

// Minutes since midnight of an Open-Meteo local time "YYYY-MM-DDTHH:MM", -1 if it is not one
// (Open-Meteo answers null, i.e. no string at all, for a polar day or night).
int16_t minutesOfLocalTime(const char *iso) {
  if (!iso || strlen(iso) < 16 || iso[10] != 'T' || iso[13] != ':') return -1;
  for (int i : {11, 12, 14, 15}) {
    if (iso[i] < '0' || iso[i] > '9') return -1;
  }
  int h = (iso[11] - '0') * 10 + (iso[12] - '0');
  int m = (iso[14] - '0') * 10 + (iso[15] - '0');
  return (h > 23 || m > 59) ? -1 : (int16_t)(h * 60 + m);
}

// Keeps only what is used: the answer is ~1.5 KB of text, the filtered document a lot less.
void buildFilter(JsonDocument &filter) {
  filter["utc_offset_seconds"] = true;
  JsonObject cur = filter["current"].to<JsonObject>();
  cur["temperature_2m"] = true;
  cur["apparent_temperature"] = true;
  cur["relative_humidity_2m"] = true;
  cur["surface_pressure"] = true;
  cur["wind_speed_10m"] = true;
  cur["weather_code"] = true;
  cur["is_day"] = true;
  JsonObject day = filter["daily"].to<JsonObject>();
  day["time"][0] = true;  // a one-element array filters every element
  day["weather_code"][0] = true;
  day["temperature_2m_max"][0] = true;
  day["temperature_2m_min"][0] = true;
  day["sunrise"][0] = true;
  day["sunset"][0] = true;
}

// Fills `out` from the parsed document. Returns false when the answer is not usable.
bool extract(JsonDocument &doc, Data &out) {
  JsonObjectConst cur = doc["current"];
  JsonObjectConst daily = doc["daily"];
  if (cur.isNull() || daily.isNull()) return false;
  if (!doc["utc_offset_seconds"].is<long>() || !cur["temperature_2m"].is<float>() ||
      !cur["weather_code"].is<int>())
    return false;

  JsonArrayConst dTime = daily["time"];
  JsonArrayConst dCode = daily["weather_code"];
  JsonArrayConst dMax = daily["temperature_2m_max"];
  JsonArrayConst dMin = daily["temperature_2m_min"];
  if (dTime.size() < 4 || dCode.size() < 4 || dMax.size() < 4 || dMin.size() < 4) return false;
  for (int i = 0; i < 4; i++) {
    if (!dMax[i].is<float>() || !dMin[i].is<float>() || !dCode[i].is<int>()) return false;
  }

  memset(&out, 0, sizeof(out));
  out.utcOffsetSeconds = doc["utc_offset_seconds"].as<int32_t>();
  out.tempC = cur["temperature_2m"].as<float>();
  out.feelsC = cur["apparent_temperature"] | out.tempC;
  out.humidity = (uint8_t)constrain(cur["relative_humidity_2m"] | 0, 0, 100);
  out.pressureHpa = cur["surface_pressure"] | 0.0f;
  out.windKmh = cur["wind_speed_10m"] | 0.0f;
  out.code = (uint8_t)(cur["weather_code"] | 0);
  out.isDay = (cur["is_day"] | 1) != 0;
  out.todayMinC = dMin[0].as<float>();
  out.todayMaxC = dMax[0].as<float>();
  out.sunriseMin = minutesOfLocalTime(daily["sunrise"][0] | "");   // optional: never fails the answer
  out.sunsetMin = minutesOfLocalTime(daily["sunset"][0] | "");
  for (int i = 0; i < 3; i++) {
    Day &d = out.forecast[i];
    d.code = (uint8_t)(dCode[i + 1] | 0);
    d.minC = dMin[i + 1].as<float>();
    d.maxC = dMax[i + 1].as<float>();
    int8_t wd = weekdayOf(dTime[i + 1] | "");
    d.wday = wd < 0 ? 0 : (uint8_t)wd;
  }
  out.valid = true;
  out.offsetValid = true;
  out.updatedAtMs = millis() ? millis() : 1;
  return true;
}

enum Outcome : uint8_t { OK, FAILED, NO_MEMORY };

Outcome fetchOnce() {
  const settings::Settings &s = settings::get();

  // The GIF decoder (~24 KB in one block) and the HTTP client + JSON parse never coexist:
  // close it first, the weather screen reopens it once the data is in.
  if (media::gifIsOpen()) media::gifClose();
  uint32_t block = ESP.getMaxFreeBlockSize();
  if (block < MIN_FREE_BLOCK) {
    fail(PSTR("heap too low (%d B)"), (int)block);
    return NO_MEMORY;
  }

  char lat[16], lon[16];
  units::formatFixed(lat, sizeof(lat), s.lat, 4);
  units::formatFixed(lon, sizeof(lon), s.lon, 4);
  String url;
  url.reserve(300);
  url += F("http://api.open-meteo.com/v1/forecast?latitude=");
  url += lat;
  url += F("&longitude=");
  url += lon;
  url += F("&current=temperature_2m,apparent_temperature,relative_humidity_2m,surface_pressure,"
           "wind_speed_10m,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&timezone=auto&forecast_days=4&wind_speed_unit=kmh");

  // HTTPClient::begin(client, url) keeps a CLONE of `client` and connects that one: this
  // local object never gets a connection. Everything is read through http.getStreamPtr();
  // reading `client` yields nothing, which is exactly what made every fetch fail with
  // "EmptyInput" in 0.3.0-rc.1 and rc.2.
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked encoding: the stream is the raw body
  http.setReuse(false);  // one request, then the connection is closed ("Connection: close")
  if (!http.begin(client, url)) {
    fail(PSTR("could not start the request"));
    return FAILED;
  }
  int code = http.GET();
  dg.httpCode = (int16_t)code;
  if (code != HTTP_CODE_OK) {
    http.end();
    if (code > 0) fail(PSTR("HTTP %d"), code);
    else if (code == -1) fail(PSTR("connection failed"));
    else if (code == -11) fail(PSTR("no answer (timeout)"));
    else fail(PSTR("network error %d"), code);
    return FAILED;
  }

  // The answer is read whole through HTTPClient's own loop (it ends when the server closes the
  // connection) and parsed from memory. Do NOT replace this with a hand-written available()/read()
  // loop over getStreamPtr(): on the real device that one saw the transfer end after one or two
  // TCP segments (414 or 950 body bytes of ~1120, "IncompleteInput"), while this call gets it all.
  // The cause was never pinned down (see "Known pitfalls" in CLAUDE.md).
  BodySink sink(MAX_BODY);
  sink.text.reserve(1280);
  http.writeToPrint(&sink);
  http.end();
  const String &body = sink.text;
  if (sink.overflow) {
    fail(PSTR("answer too large"));
    return FAILED;
  }

  Data fresh;
  bool good = false;
  {
    JsonDocument filter;
    buildFilter(filter);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    good = !err && extract(doc, fresh);
    if (err) {
      // The length tells a truncated answer from a malformed one (the whole answer is ~1.1 KB).
      fail(PSTR("bad JSON: %s (%u B)"), err.c_str(), (unsigned)body.length());
    } else if (!good) {
      fail(doc.isNull() ? PSTR("answer is not JSON") : PSTR("unexpected answer"));
    }
  }
  if (!good) return FAILED;

  cache = fresh;
  haveLocation = true;
  locLat = s.lat;
  locLon = s.lon;
  return OK;
}

}  // namespace

void begin() { forgetData(); }

void loop() {
  if (!settings::hasCity()) {
    if (cache.valid || haveLocation) forgetData();  // the city was cleared (factory reset)
    notice(PSTR("no city"));
    return;
  }
  // The location changed without requestRefresh() being called: the cache is not ours any more.
  if (haveLocation && !sameLocation()) {
    forgetData();
    forceNow = true;
  }
  if (!net::isConnected() || WiFi.status() != WL_CONNECTED) {
    if (!dg.error[0] || strcmp_P(dg.error, PSTR("no city")) == 0) notice(PSTR("no Wi-Fi"));
    return;
  }

  uint32_t now = millis();
  if (!forceNow && attempted) {
    uint32_t wait = lastOk ? (uint32_t)settings::get().weatherInterval * 60000UL
                           : (lastWasMemorySkip ? NO_MEMORY_RETRY_MS : FAILURE_BACKOFF_MS);
    if (now - lastAttemptMs < wait) return;
  }
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

const Data &data() { return cache; }

const Diag &diag() { return dg; }

void requestRefresh() {
  const settings::Settings &s = settings::get();
  if (!settings::hasCity() || !haveLocation || s.lat != locLat || s.lon != locLon) forgetData();
  forceNow = true;
}

}  // namespace weather
