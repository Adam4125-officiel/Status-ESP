// Date arithmetic for the countdown theme: pure functions, no Arduino, so they can be tested
// on a PC. Everything is "naive local time": seconds counted as if the local clock were UTC,
// which is what timekeeping::localTime() hands out (UTC plus the offset). A day is always 86400
// seconds because no daylight-saving jump is ever applied by this firmware itself: the offset
// comes from Open-Meteo and is re-read at every weather update.
#pragma once

#include <stdint.h>

namespace countdown {

// Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant's algorithm).
inline int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Naive local seconds since the epoch of a calendar date and a time of day.
inline int64_t naiveSeconds(int y, int m, int d, int hour, int minute, int second) {
  return (int64_t)daysFromCivil(y, m, d) * 86400 + hour * 3600 + minute * 60 + second;
}

// Weekday of a date, 0 = Sunday (1970-01-01 was a Thursday).
inline int weekday(int y, int m, int d) {
  int w = (int)((daysFromCivil(y, m, d) + 4) % 7);
  return w < 0 ? w + 7 : w;
}

}  // namespace countdown
