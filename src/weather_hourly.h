// Reads the hourly part of Open-Meteo's answer into weather::Data's hours.
//
// Pure (ArduinoJson and plain integers, no Arduino, no network), so tests/host builds it on a PC and
// feeds it garbage: it reads what another machine sent. The hourly forecast is optional: an answer
// without it, or with unusable entries, gives fewer hours (or none), never a failed weather update.
#pragma once

#include <ArduinoJson.h>

#include "weather.h"

namespace weather {

// Reads `hourly` (the "hourly" object of the answer: time, temperature_2m, weather_code,
// precipitation_probability, is_day) into out[0..cap). Returns how many hours were read, which is the
// longest usable run from the start: it stops at the first hour without a temperature or a weather code.
// `firstHour` is the local hour (0..23) of the first one, HOUR_UNKNOWN (and 0 returned) when the first time
// is not "YYYY-MM-DDTHH:MM".
uint8_t extractHourly(JsonObjectConst hourly, Hour *out, uint8_t cap, uint8_t &firstHour);

}  // namespace weather
