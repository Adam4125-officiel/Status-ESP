// Persistent settings: one in-RAM struct, loaded from / saved to /custom.json.
//
// - The file is plain JSON (ArduinoJson). The same JSON shape is used by the file
//   and by GET/POST /api/settings, so there is one serializer and one validator.
// - Every value is validated and clamped in apply(): unknown keys are ignored,
//   a value of the wrong type or an out-of-range string is ignored, numbers are
//   clamped. A missing or unreadable file gives the defaults.
// - Strings live in fixed char arrays (no heap fragmentation, no String copies).
// - Writes are rare: save() is called only when the user saves, never on a timer.
// - Keys "brt" and "blinv" are the ones written by firmware 0.1.0; they must not change.
//
// JSON keys (all optional in a POST; booleans are accepted as true/false or 0/1 and
// are written as 0/1):
//   brt 0..100            blinv 0|1
//   boot_delay 0..120 (s)
//   city "", lat, lon     w_unit 0 km/h|1 m/s|2 mph     t_unit 0 C|1 F
//   p_unit 0 hPa|1 kPa|2 mmHg|3 inHg      w_interval 10..180 (min)     weather_gif "" or file in /gif
//   tz_auto 0|1           tz_offset minutes -720..840 (multiple of 15)
//   hc mc sc "#RRGGBB"    hour12 0|1    date_fmt 0 DD/MM/YYYY|1 MM/DD/YYYY|2 YYYY-MM-DD
//   colon 0|1             font 0 digital (Font 7)|1 plain (Font 6)|2 large (Font 8, clock theme only)     ntp "" or host name
//   album_auto 0|1        album_interval 2..3600 (s)    album_file "" or file in /image
//   theme "weather_clock"|"forecast"|"album"|"clock"|"analog"|"digital2"|"simple_weather"|"countdown"|
//         "words"|"rings"|"binary"|"words"|"rings"|"binary"
//   auto_switch 0|1       auto_interval 5..3600 (s)     auto_themes ["clock", ...]
//   night_en 0|1          night_start "HH:MM"           night_end "HH:MM"     night_brt 0..100
//   cd_date "" (no countdown) or "YYYY-MM-DD" (2000..2099)   cd_time "HH:MM" (default 00:00)
//   cd_label "" or up to 20 printable ASCII characters
//   pw "" or 4..32 printable ASCII characters: the web password (user "admin"), "" = none.
//       Write-only: it is stored in /custom.json but never returned by toJson() unless the
//       caller asks for secrets (only save() does), so no API response and no export has it.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace settings {

enum Theme : uint8_t {
  THEME_WEATHER_CLOCK = 0,
  THEME_FORECAST = 1,
  THEME_ALBUM = 2,
  THEME_CLOCK = 3,
  THEME_ANALOG = 4,
  THEME_DIGITAL2 = 5,
  THEME_SIMPLE_WEATHER = 6,
  THEME_COUNTDOWN = 7,
  THEME_WORDS = 8,
  THEME_RINGS = 9,
  THEME_BINARY = 10,
  THEME_COUNT = 11       // autoMask is a uint16_t: at most 16 themes, this is the limit
};
enum WindUnit : uint8_t { WIND_KMH = 0, WIND_MS = 1, WIND_MPH = 2 };
enum TempUnit : uint8_t { TEMP_C = 0, TEMP_F = 1 };
enum PressUnit : uint8_t { PRESS_HPA = 0, PRESS_KPA = 1, PRESS_MMHG = 2, PRESS_INHG = 3 };
enum DateFormat : uint8_t { DATE_DMY = 0, DATE_MDY = 1, DATE_YMD = 2 };
// FONT_LARGE (Font 8, 75 px digits) only exists in the clock theme; the other themes draw it as FONT_DIGITAL.
enum ClockFont : uint8_t { FONT_DIGITAL = 0, FONT_PLAIN = 1, FONT_LARGE = 2 };

struct Settings {
  // Display
  uint8_t brightness;        // brt, percent
  bool blInverted;           // blinv, backlight polarity
  // Network
  uint16_t bootDelay;        // boot_delay, seconds waited before connecting to Wi-Fi
  // Weather
  char city[41];             // ASCII only; "" = no city chosen
  float lat, lon;
  uint8_t windUnit, tempUnit, pressUnit;
  uint16_t weatherInterval;  // w_interval, minutes
  char weatherGif[32];       // file name inside /gif, "" = none
  // Time
  bool tzAuto;               // true: UTC offset reported by Open-Meteo for the chosen city
  int16_t tzOffsetMin;       // manual offset in minutes
  uint32_t hourRgb, minRgb, secRgb;   // 0xRRGGBB
  bool hour12;
  uint8_t dateFormat;
  bool colonBlink;
  uint8_t clockFont;
  char ntpServer[41];        // "" = built-in servers only
  // Pictures
  bool albumAuto;            // cycle through the pictures; off = keep showing albumFile
  uint16_t albumInterval;    // seconds per JPG
  char albumFile[32];        // picture shown when albumAuto is off ("" = the first one)
  // Themes
  uint8_t theme;             // manual choice (a Theme)
  bool autoSwitch;
  uint16_t autoInterval;     // seconds per theme
  uint16_t autoMask;         // bit n set = Theme n takes part in the rotation
  // Night mode
  bool nightEnabled;
  uint16_t nightStart, nightEnd;      // minutes since local midnight
  uint8_t nightBrightness;
  // Countdown theme: the target is local time (the clock's own offset). cdYear == 0 = none.
  uint16_t cdYear;
  uint8_t cdMonth, cdDay;
  uint16_t cdMinutes;        // minutes since local midnight
  char cdLabel[21];
  // Security
  char password[33];         // pw, "" = the web interface and the API are open
};

// Bits returned by apply(): which groups of settings changed.
enum : uint32_t {
  CH_BRIGHTNESS = 1u << 0,   // brt, blinv
  CH_THEME = 1u << 1,        // theme, auto_switch, auto_interval, auto_themes
  CH_NIGHT = 1u << 2,
  CH_CLOCK = 1u << 3,        // colours, hour12, date_fmt, colon, font
  CH_TIMEZONE = 1u << 4,     // tz_auto, tz_offset
  CH_NTP = 1u << 5,
  CH_LOCATION = 1u << 6,     // city, lat, lon
  CH_WEATHER = 1u << 7,      // units, w_interval, weather_gif
  CH_ALBUM = 1u << 8,
  CH_BOOT_DELAY = 1u << 9,
  CH_AUTH = 1u << 10,        // pw
  CH_COUNTDOWN = 1u << 11,   // cd_date, cd_time, cd_label
  CH_VISUAL = CH_THEME | CH_CLOCK | CH_TIMEZONE | CH_LOCATION | CH_WEATHER | CH_ALBUM | CH_COUNTDOWN
};

// Mounts LittleFS WITHOUT ever formatting it (the stock files must survive), then
// loads /custom.json. Returns whether LittleFS mounted.
bool begin();
bool fsMounted();

Settings &get();

// Validates and applies a (possibly partial) JSON object. Returns the CH_* bits that changed.
uint32_t apply(JsonObjectConst obj);
// Writes every setting into doc (the same shape the file uses). The password is only written
// when `secrets` is true, which is for the file on the device and nothing else.
void toJson(JsonDocument &doc, bool secrets = false);
// Writes /custom.json (temp file + rename). Returns false if it could not be written.
bool save();
// Deletes /custom.json (only that file) and resets the RAM copy to the defaults.
bool factoryReset();

// Helpers
const char *themeName(uint8_t theme);
inline bool hasCity() { return get().city[0] != '\0'; }
inline uint16_t color565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
// 4..32 printable ASCII characters (what the web interface accepts as a password).
bool validPassword(const char *pw);
// Validates a file name coming from outside (settings, API): [A-Za-z0-9._ -], no "..", no
// leading dot or edge space, at most config::MAX_FILE_NAME characters.
bool validFileName(const char *name);

}  // namespace settings
