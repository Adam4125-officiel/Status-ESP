// Host test of the hourly forecast: the parser that reads Open-Meteo's hourly arrays (weather_hourly.cpp) and
// the theme that draws them (screen_hourly.cpp), against the recording stand-in for the display
// (TFT_eSPI.h next to this file). Built and run by tools/test_host.sh with the sanitizers on.
//
// What the stand-in can say: what was drawn, that an unchanged screen makes no drawing call, and that no text
// or line leaves the 240x240 screen or is wider than its padding. It cannot say how anything looks: set
// PORTAL_OPS_DIR=<dir> and every scenario also leaves a <name>.ops file (one JSON line per drawing call) that
// a script can turn into a picture. The icons are stubbed (icons.cpp needs more of TFT_eSPI than the stand-in
// has): each one is logged as an "icon" operation.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>

#include <ArduinoJson.h>

#include "config.h"
#include "display.h"
#include "icons.h"
#include "settings.h"
#include "timekeeping.h"
#include "weather.h"
#include "weather_hourly.h"
#include "weather_notice.h"

// ---- What the screen links against ---------------------------------------------------------------

TFT_eSPI tft;

static uint32_t g_ms = 1000000;
uint32_t millis() { return g_ms; }

static settings::Settings g_settings;
namespace settings {
Settings &get() { return g_settings; }
}  // namespace settings

static weather::Data g_weather;
namespace weather {
const Data &data() { return g_weather; }
}  // namespace weather

static weather_notice::Kind g_kind = weather_notice::NONE;
namespace weather_notice {
Kind current() { return g_kind; }
void draw(Kind) { display::drawMessage("A notice", "from weather_notice"); }
}  // namespace weather_notice

static bool g_synced = true;
static time_t g_utc = 0;
namespace timekeeping {
bool synced() { return g_synced; }
time_t utcNow() { return g_utc; }
}  // namespace timekeeping

static long g_icons = 0;
namespace icons {
void drawWeather(int16_t cx, int16_t cy, int16_t size, uint8_t code, bool isDay) {
  g_icons++;
  tft.ops++;
  if (cx - size / 2 < 0 || cx + size / 2 > 240 || cy - size / 2 < 0 || cy + size / 2 > 240) tft.violations++;
  if (tft.log) fprintf(tft.log, "{\"op\":\"icon\",\"cx\":%d,\"cy\":%d,\"size\":%d,\"code\":%u,\"day\":%d}\n", cx, cy, size, code, isDay);
}
}  // namespace icons

// The helpers of display.cpp the screen uses, copied: display.cpp itself needs the whole firmware.
namespace display {
void drawMessage(const char *title, const char *detail, uint16_t color) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  tft.drawString(title, 120, 96, tft.textWidth(title, 4) <= 232 ? 4 : 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(detail, 120, 132, 2);
  tft.setTextPadding(0);
}
void drawDegree(int16_t x, int16_t y, int16_t radius, uint16_t color) { tft.drawCircle(x, y, radius, color); }
}  // namespace display

// ---- Test plumbing ---------------------------------------------------------------------------------

static int failures = 0, checks = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    checks++;                                                            \
    if (!(cond)) {                                                       \
      failures++;                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                    \
  } while (0)

static bool drew(const char *s) {
  for (const std::string &d : tft.drawn)
    if (d == s) return true;
  return false;
}

static void expectClean(const char *scenario) {
  checks++;
  if (tft.violations) {
    failures++;
    printf("FAIL %s: %d layout problem(s)\n", scenario, tft.violations);
    for (const std::string &p : tft.problems) printf("   %s\n", p.c_str());
  }
}

static const char *g_opsDir = nullptr;
static void beginFrame(const char *name) {
  if (tft.log) {
    fclose(tft.log);
    tft.log = nullptr;
  }
  if (!g_opsDir) return;
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ops", g_opsDir, name);
  tft.log = fopen(path, "w");
}

// ---- The parser ----------------------------------------------------------------------------------

static uint8_t parse(const char *json, weather::Hour *out, uint8_t cap, uint8_t &first) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return 0xEE;   // not JSON: the caller never gets here in a real answer
  return weather::extractHourly(doc["hourly"], out, cap, first);
}

static void parserTests() {
  weather::Hour h[weather::HOURLY_MAX];
  uint8_t first;

  // A normal answer: forecast_hours=24 starts at the hour the answer was made in.
  std::string t, temp, code, rain, day;
  for (int i = 0; i < 24; i++) {
    char b[32];
    snprintf(b, sizeof(b), "%s\"2026-10-08T%02d:00\"", i ? "," : "", (16 + i) % 24);
    t += b;
    snprintf(b, sizeof(b), "%s%d.%d", i ? "," : "", 10 + i % 7, i % 10);
    temp += b;
    snprintf(b, sizeof(b), "%s%d", i ? "," : "", i % 4 ? 3 : 61);
    code += b;
    snprintf(b, sizeof(b), "%s%d", i ? "," : "", i * 4 % 101);
    rain += b;
    snprintf(b, sizeof(b), "%s%d", i ? "," : "", (16 + i) % 24 >= 7 && (16 + i) % 24 < 19);
    day += b;
  }
  std::string full = "{\"hourly\":{\"time\":[" + t + "],\"temperature_2m\":[" + temp + "],\"weather_code\":[" + code +
                     "],\"precipitation_probability\":[" + rain + "],\"is_day\":[" + day + "]}}";
  CHECK(parse(full.c_str(), h, 24, first) == 24);
  CHECK(first == 16);
  CHECK(h[0].tempDc == 100 && h[1].tempDc == 111 && h[6].tempDc == 166);
  CHECK(h[0].code == 61 && h[1].code == 3);
  CHECK(h[0].rain == 0 && h[1].rain == 4 && h[25 % 24].rain == 4);
  // is_day was built from the hour of the day: 16h is day, 19h and 00h are night, 07h is day again.
  CHECK(h[0].isDay == 1 && h[3].isDay == 0 && h[8].isDay == 0 && h[15].isDay == 1);

  // The cap, in both directions.
  CHECK(parse(full.c_str(), h, 5, first) == 5);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\"],\"temperature_2m\":[1],\"weather_code\":[0]}}", h, 24, first) == 1);

  // Optional parts: no rain and no is_day arrays, a null rain.
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\",\"2026-10-08T08:00\"],\"temperature_2m\":[1.0,2.0],"
              "\"weather_code\":[0,1],\"precipitation_probability\":[null,30]}}", h, 24, first) == 2);
  CHECK(h[0].rain == weather::RAIN_UNKNOWN && h[1].rain == 30 && h[0].isDay == 1);

  // Rounding to tenths, and negative values.
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T00:00\"],\"temperature_2m\":[-0.04],\"weather_code\":[0]}}", h, 24, first) == 1);
  CHECK(h[0].tempDc == 0);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T00:00\"],\"temperature_2m\":[-12.36],\"weather_code\":[0]}}", h, 24, first) == 1);
  CHECK(h[0].tempDc == -124);

  // Anything unusable stops the run at that hour; what came before is kept.
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\",\"2026-10-08T08:00\",\"2026-10-08T09:00\"],"
              "\"temperature_2m\":[1.0,null,3.0],\"weather_code\":[0,1,2]}}", h, 24, first) == 1);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\",\"2026-10-08T08:00\"],\"temperature_2m\":[1.0,\"warm\"],"
              "\"weather_code\":[0,1]}}", h, 24, first) == 1);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\",\"2026-10-08T08:00\"],\"temperature_2m\":[1.0,2.0],"
              "\"weather_code\":[0,300]}}", h, 24, first) == 1);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\"],\"temperature_2m\":[1500.0],\"weather_code\":[0]}}", h, 24, first) == 0);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\",\"2026-10-08T08:00\"],\"temperature_2m\":[1.0],"
              "\"weather_code\":[0,1]}}", h, 24, first) == 1);   // arrays of different lengths: the shortest wins
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T07:00\"],\"temperature_2m\":[1.0],\"weather_code\":[0],"
              "\"precipitation_probability\":[150]}}", h, 24, first) == 1);
  CHECK(h[0].rain == weather::RAIN_UNKNOWN);   // a percentage outside 0..100 is no percentage

  // No hourly part, or one without a usable first time: no hours, no hour of the day.
  CHECK(parse("{}", h, 24, first) == 0 && first == weather::HOUR_UNKNOWN);
  CHECK(parse("{\"hourly\":null}", h, 24, first) == 0 && first == weather::HOUR_UNKNOWN);
  CHECK(parse("{\"hourly\":{}}", h, 24, first) == 0 && first == weather::HOUR_UNKNOWN);
  CHECK(parse("{\"hourly\":{\"time\":[\"yesterday\"],\"temperature_2m\":[1.0],\"weather_code\":[0]}}", h, 24, first) == 0);
  CHECK(parse("{\"hourly\":{\"time\":[\"2026-10-08T25:00\"],\"temperature_2m\":[1.0],\"weather_code\":[0]}}", h, 24, first) == 0);
  CHECK(parse("{\"hourly\":{\"time\":[],\"temperature_2m\":[1.0],\"weather_code\":[0]}}", h, 24, first) == 0);
  CHECK(parse("{\"hourly\":[1,2,3]}", h, 24, first) == 0);
  CHECK(parse("{\"hourly\":\"text\"}", h, 24, first) == 0);

  // A fuzzer: the real answer, with bytes flipped. It must never crash or write out of bounds (the
  // sanitizers are what fail here), and what it returns must stay inside the cap.
  srand(7);
  int accepted = 0;
  for (int round = 0; round < 6000; round++) {
    std::string mutated = full;
    int flips = 1 + rand() % 4;
    for (int f = 0; f < flips; f++) mutated[rand() % mutated.size()] = (char)(32 + rand() % 95);
    uint8_t n = parse(mutated.c_str(), h, 24, first);
    if (n == 0xEE) continue;
    CHECK(n <= 24);
    CHECK((n == 0) == (first == weather::HOUR_UNKNOWN));
    accepted++;
  }
  printf("hourly fuzz: %d answers still JSON\n", accepted);
}

// ---- The screen ----------------------------------------------------------------------------------

// 2026-10-08T16:15 in a city at UTC+2 is 14:15 UTC.
static const time_t T0 = 1791468900;   // 2026-10-08T14:15:00Z

static void defaults() {
  // The plastic over the glass: nothing in the chart may come within 16 px of the bottom, and its labels stay 8 px
  // from the sides (the owner saw half of the hour labels when they were drawn at y 222..238).
  tft.safeBottom = 224;
  tft.safeSide = 8;
  tft.safeSideBelow = 120;
  memset(&g_settings, 0, sizeof(g_settings));
  strlcpy(g_settings.city, "Paris", sizeof(g_settings.city));
  g_settings.tempUnit = settings::TEMP_C;
  memset(&g_weather, 0, sizeof(g_weather));
  g_kind = weather_notice::NONE;
  g_ms = 1000000;
  g_synced = true;
  g_utc = T0;
  g_icons = 0;
}

// A day: temperature shaped like a day, rain chance rising in the evening.
static void install(uint8_t hourN = 24, int firstHour = 16) {
  weather::Data &w = g_weather;
  memset(&w, 0, sizeof(w));
  w.valid = w.offsetValid = true;
  w.utcOffsetSeconds = 7200;
  w.updatedAtMs = g_ms;
  w.tempC = 14.2f;
  w.hourN = hourN;
  w.hourFirst = (uint8_t)firstHour;
  for (int i = 0; i < hourN; i++) {
    int hour = (firstHour + i) % 24;
    float celsius = 14.0f - 4.0f * (hour >= 16 || hour < 6 ? ((hour + 8) % 24) / 14.0f : 1.0f) + (hour >= 9 && hour <= 17 ? (hour - 7) * 0.9f : 0.0f);
    weather::Hour &h = w.hours[i];
    h.tempDc = (int16_t)(celsius * 10);
    h.code = i % 5 == 2 ? 61 : (i % 7 == 3 ? 0 : 3);
    h.rain = (uint8_t)(i % 5 == 2 ? 80 : (i % 6) * 8);
    h.isDay = hour >= 7 && hour < 19;
  }
}

typedef void (*EnterFn)();
typedef void (*UpdateFn)(bool);
static void show(const char *name) {
  beginFrame(name);
  tft.resetStats();
  screenHourlyEnter();
  tft.fillScreen(TFT_BLACK);
  screenHourlyUpdate(true);
}

static void screenTests() {
  // The normal case.
  defaults();
  install();
  show("hourly_normal");
  expectClean("normal");
  CHECK(drew("Paris"));
  CHECK(drew("16h") && drew("17h") && drew("18h") && drew("19h") && drew("20h") && drew("21h"));   // the strip
  CHECK(drew("22h") && drew("04h") && drew("10h"));   // the chart's ticks, every sixth hour from 16h
  CHECK(g_icons == 6);
  CHECK(drew("80%"));

  // An unchanged screen makes no drawing call, and a new weather update redraws.
  tft.resetStats();
  screenHourlyUpdate(false);
  CHECK(tft.ops == 0);
  g_ms += 600000;
  g_weather.updatedAtMs = g_ms;
  tft.resetStats();
  screenHourlyUpdate(false);
  CHECK(tft.ops > 0);
  expectClean("redraw on a new answer");

  // An hour goes by: the strip starts at the next hour, and the first one is gone.
  g_utc = T0 + 3600;
  tft.resetStats();
  screenHourlyUpdate(false);
  CHECK(tft.ops > 0);
  CHECK(!drew("16h") && drew("17h") && drew("22h"));
  expectClean("an hour later");
  tft.resetStats();
  screenHourlyUpdate(false);
  CHECK(tft.ops == 0);

  // Half a day later, 12 of the 24 hours are left and the chart still fits.
  g_utc = T0 + 12 * 3600;
  show("hourly_half_a_day_later");
  expectClean("half a day later");
  CHECK(drew("04h"));

  // Every hour is behind us (a long outage): say so rather than draw the past.
  g_utc = T0 + 23 * 3600;
  install(5, 16);
  show("hourly_expired");
  expectClean("expired");
  CHECK(drew("No hourly forecast"));

  // An answer without hours (a model that has none).
  defaults();
  install(0, 0);
  g_weather.hourFirst = weather::HOUR_UNKNOWN;
  show("hourly_none");
  expectClean("none");
  CHECK(drew("No hourly forecast"));

  // Without a synced clock the age of the answer decides, and never goes past the last hour.
  defaults();
  install();
  g_synced = false;
  g_ms += 2UL * 3600000UL + 1000;
  show("hourly_unsynced");
  expectClean("unsynced");
  CHECK(!drew("16h") && !drew("17h") && drew("18h"));

  // Fahrenheit, 12-hour clock, a long name, three-digit and negative values: the widest strings there are.
  defaults();
  install();
  g_settings.tempUnit = settings::TEMP_F;
  g_settings.hour12 = true;
  strlcpy(g_settings.city, "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch", sizeof(g_settings.city));
  g_weather.tempC = 45.0f;   // 113 F
  for (int i = 0; i < 24; i++) g_weather.hours[i].tempDc = (int16_t)(i % 2 ? 450 : -400);
  for (int i = 0; i < 24; i++) g_weather.hours[i].rain = i % 3 == 0 ? 100 : weather::RAIN_UNKNOWN;
  show("hourly_widest");
  expectClean("widest strings");
  CHECK(drew("100%") && drew("-"));
  CHECK(drew("113"));

  // A flat day, rain unknown everywhere, a single hour left.
  defaults();
  install();
  for (int i = 0; i < 24; i++) { g_weather.hours[i].tempDc = 150; g_weather.hours[i].rain = weather::RAIN_UNKNOWN; }
  show("hourly_flat");
  expectClean("flat");
  defaults();
  install();
  g_utc = T0 + 19 * 3600;   // 19 hours gone: 5 left
  show("hourly_five_left");
  expectClean("five left");
  g_utc = T0 + 23 * 3600;   // 23 hours gone: 1 left
  tft.resetStats();
  screenHourlyUpdate(false);
  expectClean("one left");
  CHECK(drew("15h"));   // the one hour left is the 24th: 16h + 23

  // The notice screens (no city, loading...) are the weather screens' shared ones.
  defaults();
  install();
  g_kind = weather_notice::NO_CITY;
  show("hourly_no_city");
  expectClean("no city");
  CHECK(drew("A notice"));
  g_kind = weather_notice::NONE;
  tft.resetStats();
  screenHourlyUpdate(false);
  CHECK(tft.ops > 0 && drew("Paris"));   // data is back: the screen is drawn again
}

int main() {
  g_opsDir = getenv("PORTAL_OPS_DIR");
  parserTests();
  screenTests();
  if (tft.log) fclose(tft.log);
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
