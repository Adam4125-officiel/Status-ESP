// Time: SNTP runs in UTC and the UTC offset is applied here, so DST and the manual /
// automatic time-zone choice never reconfigure the SNTP client.
//
// Offset: settings tzAuto == true -> the offset Open-Meteo reported for the chosen city
// (weather::data().utcOffsetSeconds, DST included); false -> the manual offset. With
// Auto and no offset known yet (no city, or no weather fetched yet) the clock shows UTC
// and offsetKnown() is false, so screens can say so instead of showing a wrong time
// silently.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace timekeeping {

// Called on every loop() pass: starts SNTP once Wi-Fi is connected (never in rescue-AP
// mode), and while the clock is still unset cycles through the server lists.
void loop();
// The custom NTP server setting changed: reconfigure the SNTP client.
void applySettings();

// True once SNTP has set the clock.
bool synced();
time_t utcNow();
// Effective UTC offset in seconds (0 if unknown), and whether it is really known.
int32_t offsetSeconds();
bool offsetKnown();
// Local broken-down time. Returns false (and leaves `out` alone) until synced.
bool localTime(struct tm &out);

// Date per the user's format: "05/10/2026", "10/05/2026" or "2026-10-05".
void formatDate(char *buf, size_t size, const struct tm &t);
const char *weekdayName(int wday, bool full);   // 0 = Sunday; "Mon" / "Monday"
const char *monthName(int month, bool full);    // 0 = January; "Oct" / "October"

// Night mode: enabled in the settings AND the local time is inside [start, end), the
// window may cross midnight. False while the clock is unset.
bool inNightWindow();

}  // namespace timekeeping
