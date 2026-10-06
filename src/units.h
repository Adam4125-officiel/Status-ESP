// Unit conversion and formatting for weather values, according to the user's settings.
//
// weather::Data is always metric (deg C, km/h, hPa); screens convert at draw time with
// these helpers, so changing a unit in the web UI needs no new fetch. Labels are ASCII
// and never contain the degree sign: draw it as a small circle (display::drawDegree)
// and then draw the label.
#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "settings.h"

namespace units {

inline float temperature(float celsius) {
  return settings::get().tempUnit == settings::TEMP_F ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}
inline const char *temperatureLabel() { return settings::get().tempUnit == settings::TEMP_F ? "F" : "C"; }

inline float wind(float kmh) {
  switch (settings::get().windUnit) {
    case settings::WIND_MS: return kmh / 3.6f;
    case settings::WIND_MPH: return kmh / 1.609344f;
    default: return kmh;
  }
}
inline const char *windLabel() {
  switch (settings::get().windUnit) {
    case settings::WIND_MS: return "m/s";
    case settings::WIND_MPH: return "mph";
    default: return "km/h";
  }
}

inline float pressure(float hpa) {
  switch (settings::get().pressUnit) {
    case settings::PRESS_KPA: return hpa / 10.0f;
    case settings::PRESS_MMHG: return hpa * 0.750062f;
    case settings::PRESS_INHG: return hpa * 0.0295300f;
    default: return hpa;
  }
}
inline const char *pressureLabel() {
  switch (settings::get().pressUnit) {
    case settings::PRESS_KPA: return "kPa";
    case settings::PRESS_MMHG: return "mmHg";
    case settings::PRESS_INHG: return "inHg";
    default: return "hPa";
  }
}

// printf("%.*f") without newlib's float printf, which linking costs about 6 KB (tools/linkflags.py
// removes it from the build, so a "%f" anywhere prints nothing: use this instead).
inline void formatFixed(char *buf, size_t n, float value, uint8_t decimals) {
  long scale = 1;
  for (uint8_t i = 0; i < decimals; i++) scale *= 10;
  long scaled = lroundf(fabsf(value) * scale);
  const char *sign = (value < 0 && scaled != 0) ? "-" : "";
  if (decimals == 0) snprintf(buf, n, "%s%ld", sign, scaled);
  else snprintf(buf, n, "%s%ld.%0*ld", sign, scaled / scale, (int)decimals, scaled % scale);
}

// "-3", "21": whole degrees, no unit. Convert first with temperature().
inline void formatWhole(char *buf, size_t n, float value) {
  int v = (int)lroundf(value);
  if (v == 0) v = 0;  // never "-0"
  snprintf(buf, n, "%d", v);
}
// Pressure with the sensible number of decimals for its unit (0 hPa, 1 kPa, 0 mmHg, 2 inHg).
inline void formatPressure(char *buf, size_t n, float hpa) {
  float v = pressure(hpa);
  switch (settings::get().pressUnit) {
    case settings::PRESS_KPA: formatFixed(buf, n, v, 1); break;
    case settings::PRESS_INHG: formatFixed(buf, n, v, 2); break;
    default: snprintf(buf, n, "%d", (int)lroundf(v)); break;
  }
}

}  // namespace units
