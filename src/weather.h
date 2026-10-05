// Weather: fetch, parse and cache the current conditions and a short forecast from
// Open-Meteo (https://open-meteo.com): no API key, plain HTTP, no TLS anywhere.
//
// OWNER: the weather module. Phase 1 ships a stub (weather.cpp) so that everything
// else compiles; the real implementation must keep this interface unchanged.
//
// Threading: there are no threads. Everything runs from loop(), one call after the
// other. weather::loop() is called on every loop pass and is where the work happens.
//
// Blocking: loop() must keep answering the web server (/update must stay reachable),
// so weather::loop() returns immediately unless a fetch is due. When one is due it
// may block for at most ~5 s (HTTP timeout <= 5 s, set both on the client and on the
// connect), at most once per settings::get().weatherInterval minutes, with a 60 s
// back-off after a failure. The body must be parsed as a stream through
// an ArduinoJson filter (never loaded into a String), after http.useHTTP10(true) so
// that the server does not answer with chunked encoding (getStream() is raw).
// Check ESP.getMaxFreeBlockSize() first and skip the fetch (retry later) if it is
// too low: a GIF decoder may be alive on the screen.
//
// Request (current + 3 days after today, all metric, local time of the city):
//   http://api.open-meteo.com/v1/forecast?latitude=..&longitude=..
//     &current=temperature_2m,apparent_temperature,relative_humidity_2m,
//              surface_pressure,wind_speed_10m,weather_code,is_day
//     &daily=weather_code,temperature_2m_max,temperature_2m_min
//     &timezone=auto&forecast_days=4&wind_speed_unit=kmh
// "utc_offset_seconds" in the answer is the offset of that city right now (DST
// included): it drives the clock when the time zone is set to Auto.
#pragma once

#include <Arduino.h>

namespace weather {

struct Day {
  uint8_t code;   // WMO weather code (0 clear ... 99 thunderstorm with hail)
  uint8_t wday;   // weekday of that day, 0 = Sunday ... 6 = Saturday
  float minC;
  float maxC;
};

// Always metric: convert at draw time with units.h.
struct Data {
  bool valid;               // at least one successful fetch for the CURRENT city
  bool offsetValid;         // utcOffsetSeconds is known (true as soon as valid is)
  int32_t utcOffsetSeconds; // offset of the chosen city from UTC, DST included
  uint32_t updatedAtMs;     // millis() of the last successful fetch (0 = never)
  float tempC;
  float feelsC;
  float windKmh;
  float pressureHpa;
  uint8_t humidity;         // percent
  uint8_t code;             // WMO weather code of the current conditions
  bool isDay;
  float todayMinC;
  float todayMaxC;
  Day forecast[3];          // the three days AFTER today (tomorrow first)
};

// Called once from setup(), before the network is up. Must not touch the network.
void begin();

// Called on every loop() pass. Does nothing unless Wi-Fi is connected (not in rescue
// AP mode), a city is set (settings::hasCity()) and a fetch is due (first fetch as
// soon as possible after boot, then every weatherInterval minutes; 60 s back-off
// after a failure). See "Blocking" above.
void loop();

// Cached data. Never null, never blocks. Screens must check data().valid.
const Data &data();

// Called when the location changed (city / lat / lon) or the user asked for a fresh
// reading: discard the cached data (valid = offsetValid = false) when the location
// changed, and fetch at the next loop() pass even inside the back-off. Units need no
// refresh (Data is metric).
void requestRefresh();

}  // namespace weather
