// Host test of three themes: the weather clock (screen_weather.cpp), the forecast (screen_forecast.cpp) and the clock
// (screen_clock.cpp, both layouts), against the recording stand-in for the display (TFT_eSPI.h next to this file).
// Built and run by tools/test_host.sh with the sanitizers on.
//
// What it holds them to: the safe area (the plastic over the glass hides the last 16 px and is unkind to the sides:
// CLAUDE.md, section 12), no string or shape that leaves the screen, no text wider than its padding, no two texts
// that overlap, an unchanged screen that makes no drawing call, and, for the weather clock, the icon of the current
// conditions on the right when a GIF holds the left box. The REAL icons.cpp is linked (the weather clock's icon is
// what its test looks at); the GIF decoder and the weather are stubbed.
//
// It cannot say how anything looks: set PORTAL_OPS_DIR=<dir> and every scenario also leaves a <name>.ops file (one
// JSON line per drawing call) that a script can turn into a picture.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <set>
#include <string>

#include "config.h"
#include "display.h"
#include "icons.h"
#include "media.h"
#include "net.h"
#include "settings.h"
#include "timekeeping.h"
#include "weather.h"
#include "weather_notice.h"

// ---- What the screens link against ---------------------------------------------------------------

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

static bool g_synced = true, g_offsetKnown = true, g_ap = false;
static time_t g_localSeconds = 0;   // the local time, as if it were UTC
namespace timekeeping {
bool localTime(struct tm &out) {
  if (!g_synced) return false;
  gmtime_r(&g_localSeconds, &out);
  return true;
}
bool offsetKnown() { return g_offsetKnown; }
void formatDate(char *buf, size_t size, const struct tm &t) {
  snprintf(buf, size, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
}
const char *weekdayName(int wday, bool full) {
  static const char *const SHORT[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const LONG[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  int i = ((wday % 7) + 7) % 7;
  return full ? LONG[i] : SHORT[i];
}
}  // namespace timekeeping

namespace net {
State state() { return g_ap ? ACCESS_POINT : CONNECTED; }
}  // namespace net

// The GIF decoder: it opens when asked (unless the test says it cannot), plays a frame per pass, and draws nothing
// (the real one paints inside the 80x80 box, which no test here looks at).
static bool g_gifOk = true, g_gifOpen = false;
static int g_gifOpens = 0;
namespace media {
bool gifOpen(const char *, int16_t, int16_t, bool) {
  if (!g_gifOk) return false;
  g_gifOpen = true;
  g_gifOpens++;
  return true;
}
bool gifPlayFrame() { return g_gifOpen; }
void gifClose() { g_gifOpen = false; }
bool gifIsOpen() { return g_gifOpen; }
}  // namespace media

// The helpers of display.cpp the screens use, copied: display.cpp itself needs the whole firmware.
namespace display {
void drawFit(const char *text, int16_t y, uint16_t color, uint16_t bg) {
  tft.setTextColor(color, bg);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(238);
  tft.drawString(text, 120, y, tft.textWidth(text, 4) <= 232 ? 4 : 2);
  tft.setTextPadding(0);
}
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

static bool overlap(const TFT_Box &a, const TFT_Box &b) {
  return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

// Layout problems the stand-in flagged, and two strings drawn on top of each other (their glyph boxes).
static void expectClean(const char *scenario) {
  checks++;
  int overlaps = 0;
  for (size_t i = 0; i < tft.boxes.size(); i++)
    for (size_t j = i + 1; j < tft.boxes.size(); j++)
      if (overlap(tft.boxes[i], tft.boxes[j])) {
        if (overlaps++ < 3) tft.problems.push_back("\"" + tft.boxes[i].text + "\" overlaps \"" + tft.boxes[j].text + "\"");
      }
  if (tft.violations || overlaps) {
    failures++;
    printf("FAIL %s: %d layout problem(s), %d overlap(s)\n", scenario, tft.violations, overlaps);
    for (const std::string &p : tft.problems) printf("   %s\n", p.c_str());
  }
}

static const char *g_opsDir = nullptr;
static void beginFrame(const char *name) {
  if (tft.log) {
    fclose(tft.log);
    tft.log = nullptr;
  }
  if (!g_opsDir || !strcmp(name, "none")) return;
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ops", g_opsDir, name);
  tft.log = fopen(path, "w");
}

// 2026-11-16 (a Monday) at 14:05:07 local time.
static const time_t T0 = 1794837907;

static void defaults() {
  // The plastic over the glass: nothing within 16 px of the bottom, glyphs 8 px from the sides.
  tft.safeBottom = 224;
  tft.safeSide = 8;
  tft.safeSideBelow = 60;
  memset(&g_settings, 0, sizeof(g_settings));
  strlcpy(g_settings.city, "Paris", sizeof(g_settings.city));
  g_settings.tempUnit = settings::TEMP_C;
  g_settings.hourRgb = 0xFFFFFF;
  g_settings.minRgb = 0x00FF00;
  g_settings.secRgb = 0xFF0000;
  g_settings.clockFont = settings::FONT_DIGITAL;
  g_settings.tzAuto = true;
  memset(&g_weather, 0, sizeof(g_weather));
  g_kind = weather_notice::NONE;
  g_ms = 1000000;
  g_synced = g_offsetKnown = true;
  g_ap = false;
  g_localSeconds = T0;
  g_gifOk = true;
  g_gifOpen = false;
  g_gifOpens = 0;
}

static void install(float tempC = 18.0f, uint8_t code = 2) {
  weather::Data &w = g_weather;
  memset(&w, 0, sizeof(w));
  w.valid = w.offsetValid = true;
  w.updatedAtMs = g_ms;
  w.tempC = tempC;
  w.feelsC = tempC - 2.0f;
  w.windKmh = 14.0f;
  w.pressureHpa = 1015.0f;
  w.humidity = 62;
  w.code = code;
  w.isDay = true;
  w.todayMinC = 9.0f;
  w.todayMaxC = 21.0f;
  w.sunriseMin = 7 * 60 + 42;
  w.sunsetMin = 17 * 60 + 5;
  static const uint8_t codes[3] = {61, 3, 0};
  for (int i = 0; i < 3; i++) {
    w.forecast[i].code = codes[i];
    w.forecast[i].wday = (uint8_t)((2 + i) % 7);
    w.forecast[i].minC = 6.0f + i;
    w.forecast[i].maxC = 17.0f + i;
  }
}

typedef void (*EnterFn)();
typedef void (*UpdateFn)(bool);
static void show(const char *name, EnterFn enter, UpdateFn update) {
  beginFrame(name);
  tft.resetStats();
  enter();
  tft.fillScreen(TFT_BLACK);
  update(true);
}

// ---- The weather clock -----------------------------------------------------------------------------

static void showWeather(const char *name) { show(name, screenWeatherEnter, screenWeatherUpdate); }

// The bounding box of the icon(s) painted in the weather area above the sun row, right of the left box (x >= 90).
static bool rightIcon(TFT_Box &out) {
  bool any = false;
  out = {240, 240, 0, 0, ""};
  for (const TFT_Box &s : tft.shapes) {
    if (s.x0 < 90 || s.y1 > 160) continue;
    any = true;
    out.x0 = s.x0 < out.x0 ? s.x0 : out.x0;
    out.y0 = s.y0 < out.y0 ? s.y0 : out.y0;
    out.x1 = s.x1 > out.x1 ? s.x1 : out.x1;
    out.y1 = s.y1 > out.y1 ? s.y1 : out.y1;
  }
  return any;
}
// The same for the left box (x < 90, above the sun row).
static bool leftIcon(TFT_Box &out) {
  bool any = false;
  out = {240, 240, 0, 0, ""};
  for (const TFT_Box &s : tft.shapes) {
    if (s.x1 > 90 || s.y1 > 166) continue;
    any = true;
    out.x0 = s.x0 < out.x0 ? s.x0 : out.x0;
    out.y0 = s.y0 < out.y0 ? s.y0 : out.y0;
    out.x1 = s.x1 > out.x1 ? s.x1 : out.x1;
    out.y1 = s.y1 > out.y1 ? s.y1 : out.y1;
  }
  return any;
}

// The icon on the right is clear of every text, and 8 px from the right edge.
static void expectIconClear(const char *scenario) {
  TFT_Box icon;
  checks++;
  if (!rightIcon(icon)) {
    failures++;
    printf("FAIL %s: no icon on the right\n", scenario);
    return;
  }
  bool bad = icon.x1 > 232 || icon.y0 < 84 || icon.y1 > 134;
  const TFT_Box *hit = nullptr;
  for (const TFT_Box &b : tft.boxes)
    if (b.y0 >= 84 && overlap(icon, b)) hit = &b;
  if (bad || hit) {
    failures++;
    printf("FAIL %s: icon at x %d..%d y %d..%d%s%s\n", scenario, icon.x0, icon.x1, icon.y0, icon.y1,
           hit ? " overlaps " : "", hit ? hit->text.c_str() : "");
  }
}

static void weatherTests() {
  // No GIF chosen: the big icon is in the left box and there is no second one.
  defaults();
  install();
  showWeather("weather_no_gif");
  expectClean("weather, no GIF");
  TFT_Box icon;
  CHECK(!rightIcon(icon));
  CHECK(leftIcon(icon) && icon.x0 >= 6 && icon.x1 <= 86 && icon.y0 >= 84 && icon.y1 <= 164);
  CHECK(drew("Partly cloudy") && drew("Feels ") && drew("18") && drew("62%") && drew("07:42") && drew("17:05"));
  CHECK(!g_gifOpen);

  // An unchanged screen makes no drawing call, and a new answer repaints.
  tft.resetStats();
  screenWeatherUpdate(false);
  CHECK(tft.ops == 0);
  g_ms += 600000;
  g_weather.updatedAtMs = g_ms;
  tft.resetStats();
  screenWeatherUpdate(false);
  CHECK(tft.ops > 0);
  expectClean("weather, new answer");
  CHECK(!rightIcon(icon));

  // A GIF chosen: it plays in the left box and the icon of the conditions is on the right.
  defaults();
  install();
  strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
  showWeather("weather_gif");
  expectClean("weather, GIF");
  CHECK(g_gifOpen && g_gifOpens == 1);
  expectIconClear("weather, GIF");
  CHECK(rightIcon(icon) && icon.x1 - icon.x0 >= 28 && icon.x1 - icon.x0 <= 44);
  CHECK(!leftIcon(icon));   // the left box belongs to the GIF
  tft.resetStats();
  screenWeatherUpdate(false);   // the GIF plays a frame, nothing else is painted
  CHECK(tft.ops == 0);
  g_ms += 600000;
  g_weather.updatedAtMs = g_ms;
  g_weather.code = 63;
  tft.resetStats();
  screenWeatherUpdate(false);
  CHECK(tft.ops > 0 && drew("Rain"));
  expectClean("weather, GIF, new answer");
  expectIconClear("weather, GIF, new answer");
  CHECK(g_gifOpen);

  // The GIF cannot open (no memory, bad file): the left box falls back to the big icon, and the small one stays
  // where it was, so the layout does not jump.
  defaults();
  install();
  strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
  g_gifOk = false;
  showWeather("weather_gif_fails");
  expectClean("weather, GIF that cannot open");
  expectIconClear("weather, GIF that cannot open");
  CHECK(leftIcon(icon) && icon.x1 <= 86);

  // Night: the moon and not the sun on the small icon too.
  defaults();
  install(5.0f, 0);
  g_weather.isDay = false;
  strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
  showWeather("weather_gif_night");
  expectClean("weather, night");
  expectIconClear("weather, night");

  // The notice screens replace the lower area; the data coming back repaints it with the icon.
  defaults();
  install();
  strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
  g_kind = weather_notice::LOADING;
  showWeather("weather_notice");
  expectClean("weather, notice");
  CHECK(drew("A notice") && !rightIcon(icon));
  g_kind = weather_notice::NONE;
  tft.resetStats();
  screenWeatherUpdate(false);
  expectClean("weather, data back");
  expectIconClear("weather, data back");

  // No time yet: the clock row says so, the weather is unaffected.
  defaults();
  install();
  g_synced = false;
  strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
  showWeather("weather_unsynced");
  expectClean("weather, unsynced");
  expectIconClear("weather, unsynced");

  // The worst cases, one combination after another: every temperature from -40 to 50 C in both units, every WMO code
  // (so every description icons::describe() has), 12 and 24 hours, every wind and pressure unit, the widest
  // humidity, wind and pressure. The icon must clear every text and the safe area must hold.
  size_t longest = 0;
  std::string longestText;
  std::set<std::string> seen;
  int frames = 0;
  static const float TEMPS[] = {-40.0f, -39.6f, -12.0f, -9.0f, -1.0f, 0.0f, 9.0f, 18.0f, 37.7f, 38.0f, 45.0f, 50.0f};
  for (int unit = 0; unit < 2; unit++) {
    for (float tempC : TEMPS) {
      for (int code = 0; code < 100; code++) {
        defaults();
        install(tempC, (uint8_t)code);
        g_settings.tempUnit = unit ? settings::TEMP_F : settings::TEMP_C;
        g_settings.hour12 = (code % 2) == 1;
        g_settings.windUnit = (uint8_t)(code % 3);
        g_settings.pressUnit = (uint8_t)(code % 4);
        g_settings.clockFont = (code / 3) % 2 ? settings::FONT_PLAIN : settings::FONT_DIGITAL;
        g_weather.feelsC = tempC - 9.0f;
        g_weather.humidity = 100;
        g_weather.windKmh = 149.0f;
        g_weather.pressureHpa = 1086.0f;
        g_weather.sunriseMin = 12 * 60 + 58;   // 12:58 PM: the widest time
        g_weather.sunsetMin = 23 * 60 + 59;
        g_localSeconds = T0 + (code % 2 ? 3600 * 22 : 0);   // 12-hour: a two-digit hour
        strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
        showWeather("none");
        char label[80];
        snprintf(label, sizeof(label), "weather, %s %.1f C, code %d", unit ? "F" : "C", tempC, code);
        expectClean(label);
        expectIconClear(label);
        frames++;
        const char *d = icons::describe((uint8_t)code);
        if (seen.insert(d).second && tft.textWidth(d, 2) > (int)longest) {
          longest = (size_t)tft.textWidth(d, 2);
          longestText = d;
        }
      }
    }
  }
  printf("weather clock: %d worst-case frames, %zu descriptions, longest \"%s\" (%zu px)\n", frames, seen.size(),
         longestText.c_str(), longest);
  CHECK(seen.size() >= 25);
}

// The pictures (PORTAL_OPS_DIR): the mild case and the widest ones, with and without a GIF.
static void weatherPictures() {
  struct Pic {
    const char *name;
    float tempC;
    uint8_t code;
    bool gif, fahrenheit, hour12;
  };
  static const Pic PICS[] = {
      {"weather_gif_mild", 18.0f, 2, true, false, false},
      {"weather_gif_cold_longest", -12.0f, 86, true, false, true},
      {"weather_gif_hot_f_longest", 40.0f, 86, true, true, true},
      {"weather_gif_coldest_f", -40.0f, 99, true, true, false},
      {"weather_gif_night", 3.0f, 0, true, false, false},
      {"weather_nogif_mild", 18.0f, 2, false, false, false},
      {"weather_nogif_cold_longest", -12.0f, 86, false, false, true},
  };
  for (const Pic &p : PICS) {
    defaults();
    install(p.tempC, p.code);
    g_weather.isDay = strcmp(p.name, "weather_gif_night") != 0;
    g_settings.tempUnit = p.fahrenheit ? settings::TEMP_F : settings::TEMP_C;
    g_settings.hour12 = p.hour12;
    if (p.gif) strlcpy(g_settings.weatherGif, "cat.gif", sizeof(g_settings.weatherGif));
    showWeather(p.name);
    expectClean(p.name);
    if (p.gif) expectIconClear(p.name);
  }
}

// ---- The forecast ----------------------------------------------------------------------------------

static void showForecast(const char *name) { show(name, screenForecastEnter, screenForecastUpdate); }

static void forecastTests() {
  defaults();
  install();
  tft.safeSideBelow = 0;
  showForecast("forecast_normal");
  expectClean("forecast");
  CHECK(drew("Paris") && drew("High") && drew("Low") && drew("C"));
  CHECK(drew("Tue") && drew("Wed") && drew("Thu"));

  tft.resetStats();
  screenForecastUpdate(false);
  CHECK(tft.ops == 0);
  g_ms += 600000;
  g_weather.updatedAtMs = g_ms;
  tft.resetStats();
  screenForecastUpdate(false);
  CHECK(tft.ops > 0);
  expectClean("forecast, new answer");

  // The widest strings there are: Fahrenheit, three digits and a sign, a very long city name.
  defaults();
  install();
  tft.safeSideBelow = 0;
  g_settings.tempUnit = settings::TEMP_F;
  strlcpy(g_settings.city, "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch", sizeof(g_settings.city));
  for (int i = 0; i < 3; i++) {
    g_weather.forecast[i].maxC = i == 0 ? 50.0f : -40.0f;
    g_weather.forecast[i].minC = -40.0f;
  }
  showForecast("forecast_widest");
  expectClean("forecast, widest strings");
  CHECK(drew("122") && drew("-40"));

  // Every weekday and every code (the real icons are drawn).
  for (int code = 0; code < 100; code += 3) {
    defaults();
    install();
    tft.safeSideBelow = 0;
    for (int i = 0; i < 3; i++) g_weather.forecast[i].code = (uint8_t)(code + i);
    showForecast("none");
    expectClean("forecast, every code");
  }

  defaults();
  install();
  tft.safeSideBelow = 0;
  g_kind = weather_notice::NO_CITY;
  showForecast("forecast_notice");
  expectClean("forecast, notice");
  CHECK(drew("A notice"));
}

// ---- The clock -------------------------------------------------------------------------------------

static void showClock(const char *name) { show(name, screenClockEnter, screenClockUpdate); }

static void clockCase(const char *name, uint8_t font, bool hour12) {
  defaults();
  g_settings.clockFont = font;
  g_settings.hour12 = hour12;
  tft.safeSideBelow = font == settings::FONT_LARGE ? 120 : 60;   // the big digits of the Large font are as wide as the screen allows
  char label[80];

  // Synced, offset known: no hint.
  snprintf(label, sizeof(label), "clock %s synced", name);
  showClock(name);
  expectClean(label);
  CHECK(drew("Monday") && drew("16/11/2026"));
  CHECK(drew("07") && drew("05"));

  // Nothing changed: nothing drawn. One second later: the seconds and nothing else.
  tft.resetStats();
  screenClockUpdate(false);
  CHECK(tft.ops == 0);
  g_localSeconds += 1;
  tft.resetStats();
  screenClockUpdate(false);
  CHECK(tft.drawn.size() == 1 && tft.drawn[0] == "08");
  expectClean(label);

  // The hint lines.
  g_offsetKnown = false;
  snprintf(label, sizeof(label), "clock %s UTC hint", name);
  showClock((std::string(name) + "_utc").c_str());
  expectClean(label);
  CHECK(drew("UTC - set a city or an offset"));
  bool low = false;
  for (const TFT_Box &b : tft.boxes)
    if (b.text == "UTC - set a city or an offset") low = b.y0 >= 190 && b.y1 <= 224;
  CHECK(low);

  g_offsetKnown = true;
  g_synced = false;
  snprintf(label, sizeof(label), "clock %s syncing", name);
  showClock((std::string(name) + "_syncing").c_str());
  expectClean(label);
  CHECK(drew("Syncing the time..."));
  tft.resetStats();
  screenClockUpdate(false);
  CHECK(tft.ops == 0);

  g_ap = true;
  snprintf(label, sizeof(label), "clock %s no network", name);
  showClock((std::string(name) + "_nonet").c_str());
  expectClean(label);
  CHECK(drew("No network - time not available"));
  g_ap = false;

  // Every hour and a few widths: midnight, noon, 23:59.
  g_synced = true;
  static const int HOURS[] = {0, 1, 9, 10, 12, 13, 23};
  for (int h : HOURS) {
    g_localSeconds = T0 - 14 * 3600 - 5 * 60 - 7 + h * 3600 + 59 * 60 + 59;
    snprintf(label, sizeof(label), "clock %s at %d h", name, h);
    showClock("none");
    expectClean(label);
  }
}

static void clockTests() {
  clockCase("clock_digital", settings::FONT_DIGITAL, false);
  clockCase("clock_digital_12h", settings::FONT_DIGITAL, true);
  clockCase("clock_plain", settings::FONT_PLAIN, false);
  clockCase("clock_plain_12h", settings::FONT_PLAIN, true);
  clockCase("clock_large", settings::FONT_LARGE, false);
  clockCase("clock_large_12h", settings::FONT_LARGE, true);
}

int main() {
  g_opsDir = getenv("PORTAL_OPS_DIR");
  weatherTests();
  weatherPictures();
  forecastTests();
  clockTests();
  if (tft.log) fclose(tft.log);
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
