// Weather: Open-Meteo over plain HTTP, parsed as a stream through an ArduinoJson filter.
// See weather.h for the contract (when it may block, what it caches, the back-off).
#include "weather.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include "media.h"
#include "net.h"
#include "settings.h"

namespace weather {

namespace {

const uint32_t HTTP_TIMEOUT_MS = 5000;     // connect, headers and each stalled read
const uint32_t BODY_DEADLINE_MS = 8000;    // whole body, however slowly it trickles in
const uint32_t FAILURE_BACKOFF_MS = 60000; // after a failed fetch
const uint32_t NO_MEMORY_RETRY_MS = 15000; // heap too fragmented right now: look again soon
const uint32_t MIN_FREE_BLOCK = 9000;      // HTTP client + filtered document, with margin

Data cache;
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
  haveLocation = false;
  attempted = false;
}

// Reads from the client with a deadline for the WHOLE body: ArduinoJson's own stream reader
// would wait up to the stream timeout for every single byte of a stalled transfer.
class DeadlineStream : public Stream {
 public:
  DeadlineStream(WiFiClient &client, uint32_t budgetMs) : client_(client), deadline_(millis() + budgetMs) {}

  int available() override { return client_.available(); }
  int peek() override { return client_.peek(); }
  size_t write(uint8_t) override { return 0; }
  int read() override {
    char c;
    return readBytes(&c, 1) == 1 ? (uint8_t)c : -1;
  }
  size_t readBytes(char *buffer, size_t length) override {
    size_t got = 0;
    while (got < length) {
      int ready = client_.available();
      if (ready > 0) {
        if ((size_t)ready > length - got) ready = (int)(length - got);
        got += client_.read((uint8_t *)buffer + got, ready);
        continue;
      }
      if (!client_.connected() || (int32_t)(millis() - deadline_) >= 0) break;
      delay(1);
    }
    return got;
  }

 private:
  WiFiClient &client_;
  uint32_t deadline_;
};

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
  if (ESP.getMaxFreeBlockSize() < MIN_FREE_BLOCK) return NO_MEMORY;

  char coords[40];
  snprintf(coords, sizeof(coords), "latitude=%.4f&longitude=%.4f", s.lat, s.lon);
  String url;
  url.reserve(300);
  url += F("http://api.open-meteo.com/v1/forecast?");
  url += coords;
  url += F("&current=temperature_2m,apparent_temperature,relative_humidity_2m,surface_pressure,"
           "wind_speed_10m,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&timezone=auto&forecast_days=4&wind_speed_unit=kmh");

  WiFiClient client;
  HTTPClient http;
  client.setTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // no chunked encoding: the stream is the raw body
  if (!http.begin(client, url)) return FAILED;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf_P(PSTR("[weather] HTTP %d\n"), code);
    return FAILED;
  }

  Data fresh;
  bool good;
  {
    JsonDocument filter;
    buildFilter(filter);
    JsonDocument doc;
    DeadlineStream body(client, BODY_DEADLINE_MS);
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    http.end();
    good = !err && extract(doc, fresh);
    if (!good) Serial.printf_P(PSTR("[weather] unusable answer (%s)\n"), err.c_str());
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
    return;
  }
  // The location changed without requestRefresh() being called: the cache is not ours any more.
  if (haveLocation && !sameLocation()) {
    forgetData();
    forceNow = true;
  }
  if (!net::isConnected() || WiFi.status() != WL_CONNECTED) return;

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
}

const Data &data() { return cache; }

void requestRefresh() {
  const settings::Settings &s = settings::get();
  if (!settings::hasCity() || !haveLocation || s.lat != locLat || s.lon != locLon) forgetData();
  forceNow = true;
}

}  // namespace weather
