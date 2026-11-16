// Night mode: which minutes of the day are "night". Pure (no Arduino, no clock): the firmware feeds it the
// minutes and tests/host/test_night.cpp checks it on the PC.
//
// Two clocks are involved. The fixed window (night_start, night_end) is read on the clock the screen shows
// (time zone Auto = the city's offset, Manual = the owner's). Sunrise and sunset come from the weather answer in
// the CITY's own time, so they are compared with the city's minutes, whatever the time-zone setting says.
#pragma once

#include <stdint.h>

namespace night {

// `nowMin` is inside [startMin, endMin), a window that may cross midnight. Equal bounds: never.
inline bool contains(int nowMin, int startMin, int endMin) {
  if (startMin == endMin) return false;
  if (startMin < endMin) return nowMin >= startMin && nowMin < endMin;
  return nowMin >= startMin || nowMin < endMin;
}

// Whether sunset -> sunrise can be used: the switch is on, the weather is known (`weatherOk`, with the city's
// offset) and both times are real minutes of a day (-1 = not reported: polar day or night).
inline bool usesSun(bool sunOn, bool weatherOk, int sunriseMin, int sunsetMin) {
  return sunOn && weatherOk && sunriseMin >= 0 && sunriseMin < 1440 && sunsetMin >= 0 && sunsetMin < 1440 &&
         sunriseMin != sunsetMin;
}

// The decision. `localMin` is the minute of the day on the screen's clock, `cityMin` the same instant in the
// city's own time (only read when usesSun()).
inline bool inWindow(bool sunOn, bool weatherOk, int sunriseMin, int sunsetMin, int fixedStart, int fixedEnd,
                     int localMin, int cityMin) {
  if (usesSun(sunOn, weatherOk, sunriseMin, sunsetMin)) return contains(cityMin, sunsetMin, sunriseMin);
  return contains(localMin, fixedStart, fixedEnd);
}

}  // namespace night
