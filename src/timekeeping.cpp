#include "timekeeping.h"

#include <ESP8266WiFi.h>

#include "net.h"
#include "settings.h"
#include "weather.h"

namespace timekeeping {

// lwIP's SNTP client keeps the pointers it is given, hence static storage.
static char customHost[41] = "";
static bool started = false;
static bool useCustom = true;
static uint32_t lastServerChange = 0;
static const uint32_t SERVER_RETRY_MS = 45000;

// lwIP SNTP has 3 server slots: the custom server (when set) goes first, followed by
// the two first built-in ones (time.cloudflare.com is the default); if the clock is still
// unset after a while the lists are swapped so that all three built-in servers get a turn.
static void configure() {
  if (useCustom && customHost[0]) {
    configTime(0, 0, customHost, "time.cloudflare.com", "pool.ntp.org");
  } else {
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
  }
  lastServerChange = millis();
}

void loop() {
  if (!net::isConnected()) return;
  if (!started) {
    started = true;
    useCustom = true;
    strlcpy(customHost, settings::get().ntpServer, sizeof(customHost));
    configure();
    return;
  }
  if (!synced() && millis() - lastServerChange > SERVER_RETRY_MS) {
    useCustom = !useCustom;
    configure();
  }
}

void applySettings() {
  strlcpy(customHost, settings::get().ntpServer, sizeof(customHost));
  if (!started) return;
  useCustom = true;
  configure();
}

bool synced() { return time(nullptr) > 1700000000; }  // after Nov 2023: the clock was really set

time_t utcNow() { return time(nullptr); }

bool offsetKnown() {
  if (!settings::get().tzAuto) return true;
  return weather::data().offsetValid;
}

int32_t offsetSeconds() {
  const settings::Settings &s = settings::get();
  if (!s.tzAuto) return (int32_t)s.tzOffsetMin * 60;
  const weather::Data &w = weather::data();
  return w.offsetValid ? w.utcOffsetSeconds : 0;
}

bool localTime(struct tm &out) {
  if (!synced()) return false;
  time_t t = time(nullptr) + offsetSeconds();
  gmtime_r(&t, &out);
  return true;
}

void formatDate(char *buf, size_t size, const struct tm &t) {
  int d = t.tm_mday, m = t.tm_mon + 1, y = t.tm_year + 1900;
  switch (settings::get().dateFormat) {
    case settings::DATE_MDY: snprintf(buf, size, "%02d/%02d/%04d", m, d, y); break;
    case settings::DATE_YMD: snprintf(buf, size, "%04d-%02d-%02d", y, m, d); break;
    default: snprintf(buf, size, "%02d/%02d/%04d", d, m, y); break;
  }
}

static const char *const DAYS_FULL[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const DAYS_SHORT[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const MONTHS_FULL[12] = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
static const char *const MONTHS_SHORT[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

const char *weekdayName(int wday, bool full) {
  if (wday < 0 || wday > 6) wday = 0;
  return full ? DAYS_FULL[wday] : DAYS_SHORT[wday];
}

const char *monthName(int month, bool full) {
  if (month < 0 || month > 11) month = 0;
  return full ? MONTHS_FULL[month] : MONTHS_SHORT[month];
}

bool inNightWindow() {
  const settings::Settings &s = settings::get();
  if (!s.nightEnabled || s.nightStart == s.nightEnd) return false;
  struct tm t;
  if (!localTime(t)) return false;
  uint16_t now = (uint16_t)(t.tm_hour * 60 + t.tm_min);
  if (s.nightStart < s.nightEnd) return now >= s.nightStart && now < s.nightEnd;
  return now >= s.nightStart || now < s.nightEnd;  // crosses midnight
}

}  // namespace timekeeping
