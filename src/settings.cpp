#include "settings.h"

#include <LittleFS.h>
#include <math.h>

#include "config.h"
#include "portal_data.h"
#include "portal_url.h"

namespace settings {

static Settings S;
static bool mounted = false;

static_assert(sizeof(Settings::portalUrl) == portal::MAX_URL + 1, "portalUrl must hold exactly what portal::checkUrl accepts");
static_assert(sizeof(Settings::portalKey) == portal::MAX_KEY + 1, "portalKey must hold exactly what portal::validKey accepts");

static const char *const THEME_NAMES[THEME_COUNT] = {"weather_clock", "forecast", "album",  "clock",
                                                     "words",         "binary",   "portal", "resources"};

const char *themeName(uint8_t theme) {
  return theme < THEME_COUNT ? THEME_NAMES[theme] : THEME_NAMES[THEME_CLOCK];
}

static bool themeFromName(const char *name, uint8_t &out) {
  for (uint8_t i = 0; i < THEME_COUNT; i++) {
    if (strcmp(name, THEME_NAMES[i]) == 0) {
      out = i;
      return true;
    }
  }
  return false;
}

// --- Defaults ---------------------------------------------------------------

static void defaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.brightness = 70;
  s.blInverted = true;
  s.bootDelay = 0;
  s.windUnit = WIND_KMH;
  s.tempUnit = TEMP_C;
  s.pressUnit = PRESS_HPA;
  s.weatherInterval = 20;
  s.tzAuto = true;
  s.tzOffsetMin = 0;
  s.hourRgb = 0xFFFFFF;
  s.minRgb = 0x4FC3F7;
  s.secRgb = 0xFFB300;
  s.hour12 = false;
  s.dateFormat = DATE_DMY;
  s.colonBlink = true;
  s.clockFont = FONT_DIGITAL;
  s.albumAuto = true;
  s.albumInterval = 5;
  // Out of the box nothing needs configuring: the clock works without a city or a network.
  s.theme = THEME_CLOCK;
  s.autoSwitch = false;
  s.autoInterval = 10;
  s.autoMask = (1u << THEME_COUNT) - 1;
  s.nightEnabled = false;
  s.nightStart = 22 * 60;
  s.nightEnd = 7 * 60;
  s.nightBrightness = 10;
  s.portalInterval = 60;
  s.portalPage = 6;
  s.portalSections = portal::SEC_ALL;
  s.portalAlert = PORTAL_ALERT_INDICATOR;
}

// --- Validation helpers -------------------------------------------------------

static bool readBool(JsonVariantConst v, bool &out) {
  if (v.is<bool>()) {
    out = v.as<bool>();
    return true;
  }
  if (v.is<long>()) {
    out = v.as<long>() != 0;
    return true;
  }
  return false;
}

static bool readInt(JsonVariantConst v, long lo, long hi, long &out) {
  long x;
  if (v.is<long>()) {
    x = v.as<long>();
  } else if (v.is<float>()) {
    x = lroundf(v.as<float>());
  } else {
    return false;
  }
  out = x < lo ? lo : (x > hi ? hi : x);
  return true;
}

static bool readFloat(JsonVariantConst v, float lo, float hi, float &out) {
  if (!v.is<float>()) return false;
  float x = v.as<float>();
  if (isnan(x)) return false;
  out = x < lo ? lo : (x > hi ? hi : x);
  return true;
}

// Printable ASCII only (the display cannot draw anything else), trimmed.
static void copyText(char *dst, size_t cap, const char *src) {
  size_t n = 0;
  for (; *src && n < cap - 1; ++src) {
    unsigned char c = (unsigned char)*src;
    if (c < 32 || c > 126) continue;
    if (n == 0 && c == ' ') continue;
    dst[n++] = (char)c;
  }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = '\0';
}

bool validFileName(const char *name) {
  size_t n = name ? strlen(name) : 0;
  if (n == 0 || n > config::MAX_FILE_NAME) return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-' || c == ' ';
    if (!ok) return false;
    if (c == '.' && i + 1 < n && name[i + 1] == '.') return false;
  }
  return name[0] != '.' && name[0] != ' ' && name[n - 1] != ' ';
}

bool validPassword(const char *pw) {
  size_t n = pw ? strlen(pw) : 0;
  if (n < 4 || n > sizeof(Settings::password) - 1) return false;
  for (size_t i = 0; i < n; i++) {
    if ((uint8_t)pw[i] < 32 || (uint8_t)pw[i] > 126) return false;
  }
  return true;
}

static bool validHostName(const char *s) {
  size_t n = strlen(s);
  if (n == 0 || n > 40) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
  }
  return true;
}

// "#RRGGBB" or "RRGGBB".
static bool parseColor(const char *s, uint32_t &out) {
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    char c = s[i];
    uint8_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = (v << 4) | d;
  }
  out = v;
  return true;
}

// "HH:MM" -> minutes since midnight.
static bool parseClock(const char *s, uint16_t &out) {
  if (strlen(s) != 5 || s[2] != ':') return false;
  for (int i : {0, 1, 3, 4}) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  int h = (s[0] - '0') * 10 + (s[1] - '0');
  int m = (s[3] - '0') * 10 + (s[4] - '0');
  if (h > 23 || m > 59) return false;
  out = (uint16_t)(h * 60 + m);
  return true;
}

static uint32_t diff(const Settings &a, const Settings &b) {
  uint32_t ch = 0;
  if (a.brightness != b.brightness || a.blInverted != b.blInverted) ch |= CH_BRIGHTNESS;
  if (a.theme != b.theme || a.autoSwitch != b.autoSwitch || a.autoInterval != b.autoInterval ||
      a.autoMask != b.autoMask) ch |= CH_THEME;
  if (a.nightEnabled != b.nightEnabled || a.nightStart != b.nightStart || a.nightEnd != b.nightEnd ||
      a.nightBrightness != b.nightBrightness) ch |= CH_NIGHT;
  if (a.hourRgb != b.hourRgb || a.minRgb != b.minRgb || a.secRgb != b.secRgb || a.hour12 != b.hour12 ||
      a.dateFormat != b.dateFormat || a.colonBlink != b.colonBlink || a.clockFont != b.clockFont) ch |= CH_CLOCK;
  if (a.tzAuto != b.tzAuto || a.tzOffsetMin != b.tzOffsetMin) ch |= CH_TIMEZONE;
  if (strcmp(a.ntpServer, b.ntpServer) != 0) ch |= CH_NTP;
  if (strcmp(a.city, b.city) != 0 || fabsf(a.lat - b.lat) > 1e-6f || fabsf(a.lon - b.lon) > 1e-6f) ch |= CH_LOCATION;
  if (a.windUnit != b.windUnit || a.tempUnit != b.tempUnit || a.pressUnit != b.pressUnit ||
      a.weatherInterval != b.weatherInterval || strcmp(a.weatherGif, b.weatherGif) != 0) ch |= CH_WEATHER;
  if (a.albumAuto != b.albumAuto || a.albumInterval != b.albumInterval || strcmp(a.albumFile, b.albumFile) != 0) ch |= CH_ALBUM;
  if (a.bootDelay != b.bootDelay) ch |= CH_BOOT_DELAY;
  if (strcmp(a.password, b.password) != 0) ch |= CH_AUTH;
  if (strcmp(a.portalUrl, b.portalUrl) != 0 || strcmp(a.portalKey, b.portalKey) != 0 ||
      a.portalInterval != b.portalInterval || a.portalSections != b.portalSections ||
      a.portalAlert != b.portalAlert) ch |= CH_PORTAL;
  return ch;
}

// --- apply / toJson -----------------------------------------------------------

// The five Status-Portal switches: one JSON key per bit of Settings::portalSections.
struct PortalSwitch {
  const char *key;
  uint8_t bit;
};
static const PortalSwitch PORTAL_SWITCHES[] = {
    {"portal_services", portal::SEC_SERVICES},
    {"portal_incidents", portal::SEC_INCIDENTS},
    {"portal_maintenance", portal::SEC_MAINTENANCE},
    {"portal_resources", portal::SEC_RESOURCES},
    {"portal_announcements", portal::SEC_ANNOUNCEMENTS},
};

uint32_t apply(JsonObjectConst obj) {
  Settings old = S;
  long n;
  bool b;
  float f;
  const char *str;

  if (readInt(obj["brt"], 0, 100, n)) S.brightness = (uint8_t)n;
  if (readBool(obj["blinv"], b)) S.blInverted = b;

  if (readInt(obj["boot_delay"], 0, 120, n)) S.bootDelay = (uint16_t)n;

  if (obj["city"].is<const char *>()) copyText(S.city, sizeof(S.city), obj["city"].as<const char *>());
  if (readFloat(obj["lat"], -90.0f, 90.0f, f)) S.lat = f;
  if (readFloat(obj["lon"], -180.0f, 180.0f, f)) S.lon = f;
  if (readInt(obj["w_unit"], 0, 2, n)) S.windUnit = (uint8_t)n;
  if (readInt(obj["t_unit"], 0, 1, n)) S.tempUnit = (uint8_t)n;
  if (readInt(obj["p_unit"], 0, 3, n)) S.pressUnit = (uint8_t)n;
  if (readInt(obj["w_interval"], 10, 180, n)) S.weatherInterval = (uint16_t)n;
  if (obj["weather_gif"].is<const char *>()) {
    str = obj["weather_gif"].as<const char *>();
    if (str[0] == '\0') S.weatherGif[0] = '\0';
    else if (validFileName(str)) copyText(S.weatherGif, sizeof(S.weatherGif), str);
  }

  if (readBool(obj["tz_auto"], b)) S.tzAuto = b;
  if (readInt(obj["tz_offset"], -720, 840, n)) {
    n = (n >= 0 ? n + 7 : n - 7) / 15 * 15;  // 15-minute steps
    S.tzOffsetMin = (int16_t)n;
  }
  uint32_t rgb;
  if (obj["hc"].is<const char *>() && parseColor(obj["hc"].as<const char *>(), rgb)) S.hourRgb = rgb;
  if (obj["mc"].is<const char *>() && parseColor(obj["mc"].as<const char *>(), rgb)) S.minRgb = rgb;
  if (obj["sc"].is<const char *>() && parseColor(obj["sc"].as<const char *>(), rgb)) S.secRgb = rgb;
  if (readBool(obj["hour12"], b)) S.hour12 = b;
  if (readInt(obj["date_fmt"], 0, 2, n)) S.dateFormat = (uint8_t)n;
  if (readBool(obj["colon"], b)) S.colonBlink = b;
  if (readInt(obj["font"], 0, 2, n)) S.clockFont = (uint8_t)n;
  if (obj["ntp"].is<const char *>()) {
    str = obj["ntp"].as<const char *>();
    if (str[0] == '\0') S.ntpServer[0] = '\0';
    else if (validHostName(str)) copyText(S.ntpServer, sizeof(S.ntpServer), str);
  }

  if (readBool(obj["album_auto"], b)) S.albumAuto = b;
  if (readInt(obj["album_interval"], 2, 3600, n)) S.albumInterval = (uint16_t)n;
  if (obj["album_file"].is<const char *>()) {
    str = obj["album_file"].as<const char *>();
    if (str[0] == '\0') S.albumFile[0] = '\0';
    else if (validFileName(str)) copyText(S.albumFile, sizeof(S.albumFile), str);
  }

  uint8_t t;
  if (obj["theme"].is<const char *>() && themeFromName(obj["theme"].as<const char *>(), t)) S.theme = t;
  if (readBool(obj["auto_switch"], b)) S.autoSwitch = b;
  if (readInt(obj["auto_interval"], 5, 3600, n)) S.autoInterval = (uint16_t)n;
  if (obj["auto_themes"].is<JsonArrayConst>()) {
    uint16_t mask = 0;
    for (JsonVariantConst v : obj["auto_themes"].as<JsonArrayConst>()) {
      if (v.is<const char *>() && themeFromName(v.as<const char *>(), t)) mask |= (1u << t);
    }
    if (mask) S.autoMask = mask;  // an empty rotation makes no sense: keep the previous one
  }

  if (readBool(obj["night_en"], b)) S.nightEnabled = b;
  uint16_t minutes;
  if (obj["night_start"].is<const char *>() && parseClock(obj["night_start"].as<const char *>(), minutes)) S.nightStart = minutes;
  if (obj["night_end"].is<const char *>() && parseClock(obj["night_end"].as<const char *>(), minutes)) S.nightEnd = minutes;
  if (readInt(obj["night_brt"], 0, 100, n)) S.nightBrightness = (uint8_t)n;

  if (obj["pw"].is<const char *>()) {
    str = obj["pw"].as<const char *>();
    if (str[0] == '\0') S.password[0] = '\0';
    else if (validPassword(str)) strlcpy(S.password, str, sizeof(S.password));
  }

  if (obj["portal_url"].is<const char *>()) {
    str = obj["portal_url"].as<const char *>();
    char url[sizeof(S.portalUrl)];
    if (str[0] == '\0') S.portalUrl[0] = '\0';
    else if (portal::checkUrl(str, url, sizeof(url)) == portal::URL_OK) strlcpy(S.portalUrl, url, sizeof(S.portalUrl));
  }
  if (obj["portal_key"].is<const char *>()) {
    str = obj["portal_key"].as<const char *>();
    if (str[0] == '\0') S.portalKey[0] = '\0';
    else if (portal::validKey(str)) strlcpy(S.portalKey, str, sizeof(S.portalKey));
  }
  if (readInt(obj["portal_interval"], 30, 600, n)) S.portalInterval = (uint16_t)n;
  if (readInt(obj["portal_page"], 2, 60, n)) S.portalPage = (uint8_t)n;
  for (const PortalSwitch &sw : PORTAL_SWITCHES) {
    if (readBool(obj[sw.key], b)) S.portalSections = b ? (S.portalSections | sw.bit) : (S.portalSections & ~sw.bit);
  }
  if (obj["portal_alert"].is<const char *>()) {
    str = obj["portal_alert"].as<const char *>();
    if (strcmp(str, "off") == 0) S.portalAlert = PORTAL_ALERT_OFF;
    else if (strcmp(str, "indicator") == 0) S.portalAlert = PORTAL_ALERT_INDICATOR;
    else if (strcmp(str, "switch") == 0) S.portalAlert = PORTAL_ALERT_SWITCH;
  }

  return diff(old, S);
}

void toJson(JsonDocument &doc, bool secrets) {
  char buf[16];
  doc["brt"] = S.brightness;
  doc["blinv"] = S.blInverted ? 1 : 0;
  doc["boot_delay"] = S.bootDelay;

  doc["city"] = String(S.city);
  doc["lat"] = S.lat;
  doc["lon"] = S.lon;
  doc["w_unit"] = S.windUnit;
  doc["t_unit"] = S.tempUnit;
  doc["p_unit"] = S.pressUnit;
  doc["w_interval"] = S.weatherInterval;
  doc["weather_gif"] = String(S.weatherGif);

  doc["tz_auto"] = S.tzAuto ? 1 : 0;
  doc["tz_offset"] = S.tzOffsetMin;
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)S.hourRgb);
  doc["hc"] = String(buf);
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)S.minRgb);
  doc["mc"] = String(buf);
  snprintf(buf, sizeof(buf), "#%06lX", (unsigned long)S.secRgb);
  doc["sc"] = String(buf);
  doc["hour12"] = S.hour12 ? 1 : 0;
  doc["date_fmt"] = S.dateFormat;
  doc["colon"] = S.colonBlink ? 1 : 0;
  doc["font"] = S.clockFont;
  doc["ntp"] = String(S.ntpServer);

  doc["album_auto"] = S.albumAuto ? 1 : 0;
  doc["album_interval"] = S.albumInterval;
  doc["album_file"] = String(S.albumFile);

  doc["theme"] = themeName(S.theme);
  doc["auto_switch"] = S.autoSwitch ? 1 : 0;
  doc["auto_interval"] = S.autoInterval;
  JsonArray themes = doc["auto_themes"].to<JsonArray>();
  for (uint8_t i = 0; i < THEME_COUNT; i++) {
    if (S.autoMask & (1u << i)) themes.add(themeName(i));
  }

  doc["night_en"] = S.nightEnabled ? 1 : 0;
  snprintf(buf, sizeof(buf), "%02u:%02u", S.nightStart / 60, S.nightStart % 60);
  doc["night_start"] = String(buf);
  snprintf(buf, sizeof(buf), "%02u:%02u", S.nightEnd / 60, S.nightEnd % 60);
  doc["night_end"] = String(buf);
  doc["night_brt"] = S.nightBrightness;

  doc["portal_url"] = String(S.portalUrl);
  doc["portal_interval"] = S.portalInterval;
  doc["portal_page"] = S.portalPage;
  for (const PortalSwitch &sw : PORTAL_SWITCHES) doc[sw.key] = (S.portalSections & sw.bit) ? 1 : 0;
  doc["portal_alert"] = S.portalAlert == PORTAL_ALERT_OFF ? "off" : (S.portalAlert == PORTAL_ALERT_SWITCH ? "switch" : "indicator");

  if (secrets && S.password[0]) doc["pw"] = String(S.password);
  if (secrets && S.portalKey[0]) doc["portal_key"] = String(S.portalKey);
}

// --- Persistence ----------------------------------------------------------------

static void load() {
  if (!mounted || !LittleFS.exists(config::SETTINGS_FILE)) return;
  File f = LittleFS.open(config::SETTINGS_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || !doc.is<JsonObject>()) return;  // unreadable: keep the defaults
  apply(doc.as<JsonObjectConst>());
}

bool begin() {
  defaults(S);
  // Mount LittleFS WITHOUT ever formatting it: the stock files must survive.
  LittleFSConfig lfsCfg;
  lfsCfg.setAutoFormat(false);
  LittleFS.setConfig(lfsCfg);
  mounted = LittleFS.begin();
  load();
  return mounted;
}

bool fsMounted() { return mounted; }

Settings &get() { return S; }

bool save() {
  if (!mounted) return false;
  JsonDocument doc;
  toJson(doc, true);   // the file is the one place the password is kept
  File f = LittleFS.open(config::SETTINGS_TMP, "w");
  if (!f) return false;
  size_t written = serializeJson(doc, f);
  f.close();
  if (written == 0) {
    LittleFS.remove(config::SETTINGS_TMP);
    return false;
  }
  // rename() replaces an existing destination atomically in littlefs; the fallback
  // covers a build where it refuses.
  if (LittleFS.rename(config::SETTINGS_TMP, config::SETTINGS_FILE)) return true;
  LittleFS.remove(config::SETTINGS_FILE);
  return LittleFS.rename(config::SETTINGS_TMP, config::SETTINGS_FILE);
}

bool factoryReset() {
  bool ok = true;
  if (mounted) {
    // Only our own settings file: uploaded pictures and Wi-Fi credentials are kept.
    if (LittleFS.exists(config::SETTINGS_FILE)) ok = LittleFS.remove(config::SETTINGS_FILE);
    if (LittleFS.exists(config::SETTINGS_TMP)) LittleFS.remove(config::SETTINGS_TMP);
  }
  defaults(S);
  return ok;
}

}  // namespace settings
