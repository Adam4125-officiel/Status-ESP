#include "weather_hourly.h"

#include <math.h>
#include <string.h>

namespace weather {

namespace {

// Local hour of an Open-Meteo local time "YYYY-MM-DDTHH:MM", -1 if it is not one.
int hourOfLocalTime(const char *iso) {
  if (!iso || strlen(iso) < 16 || iso[10] != 'T' || iso[13] != ':') return -1;
  for (int i : {11, 12, 14, 15}) {
    if (iso[i] < '0' || iso[i] > '9') return -1;
  }
  int h = (iso[11] - '0') * 10 + (iso[12] - '0');
  return h > 23 ? -1 : h;
}

}  // namespace

uint8_t extractHourly(JsonObjectConst hourly, Hour *out, uint8_t cap, uint8_t &firstHour) {
  firstHour = HOUR_UNKNOWN;
  if (hourly.isNull()) return 0;
  JsonArrayConst time = hourly["time"];
  JsonArrayConst temp = hourly["temperature_2m"];
  JsonArrayConst code = hourly["weather_code"];
  JsonArrayConst rain = hourly["precipitation_probability"];
  JsonArrayConst day = hourly["is_day"];

  int first = hourOfLocalTime(time[0] | "");
  if (first < 0) return 0;

  uint8_t n = 0;
  for (; n < cap && n < temp.size() && n < code.size(); n++) {
    JsonVariantConst t = temp[n];
    JsonVariantConst c = code[n];
    if (!t.is<float>() || !c.is<int>()) break;
    float celsius = t.as<float>();
    if (!(celsius > -100.0f && celsius < 100.0f)) break;   // also rejects NaN
    int wmo = c.as<int>();
    if (wmo < 0 || wmo > 255) break;

    Hour &h = out[n];
    h.tempDc = (int16_t)lroundf(celsius * 10.0f);
    h.code = (uint8_t)wmo;
    JsonVariantConst r = rain[n];
    h.rain = RAIN_UNKNOWN;   // the models do not all give one: null or missing is "unknown", not 0
    if (r.is<float>()) {
      float percent = r.as<float>();
      if (percent >= 0.0f && percent <= 100.0f) h.rain = (uint8_t)lroundf(percent);
    }
    JsonVariantConst d = day[n];
    h.isDay = d.is<float>() ? (d.as<float>() != 0.0f) : 1;
  }
  if (n) firstHour = (uint8_t)first;
  return n;
}

}  // namespace weather
