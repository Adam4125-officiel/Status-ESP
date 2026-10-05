#include "display.h"

#include "config.h"
#include "media.h"
#include "timekeeping.h"

TFT_eSPI tft;

namespace display {

struct Screen {
  void (*enter)();
  void (*update)(bool);
  void (*leave)();
};

// Indexed by settings::Theme.
static const Screen SCREENS[settings::THEME_COUNT] = {
    {screenWeatherEnter, screenWeatherUpdate, screenWeatherLeave},      // THEME_WEATHER_CLOCK
    {screenForecastEnter, screenForecastUpdate, screenForecastLeave},   // THEME_FORECAST
    {screenAlbumEnter, screenAlbumUpdate, screenAlbumLeave},            // THEME_ALBUM
    {screenClockEnter, screenClockUpdate, screenClockLeave},            // THEME_CLOCK
};

static const uint8_t NO_THEME = 255;

enum Mode : uint8_t { MODE_STATUS, MODE_THEMES };
static Mode mode = MODE_STATUS;
static bool statusDrawn = false;
static uint32_t themesAt = 0;     // millis() deadline to start the themes (0 = never)
static uint8_t current = NO_THEME;
static bool needFull = false;
static uint32_t themeSince = 0;
static uint32_t lastBacklightCheck = 0;
static int32_t lastDuty = -1;

// --- Backlight ------------------------------------------------------------------

void applyBacklight() {
  const settings::Settings &s = settings::get();
  uint8_t percent = (s.nightEnabled && timekeeping::inNightWindow()) ? s.nightBrightness : s.brightness;
  int32_t duty = map(percent, 0, 100, 0, 1023);
  int32_t out = s.blInverted ? 1023 - duty : duty;
  if (out == lastDuty) return;
  lastDuty = out;
  analogWrite(config::PIN_BACKLIGHT, out);
}

void begin() {
  analogWriteRange(1023);
  analogWriteFreq(1000);
  lastDuty = -1;
  applyBacklight();

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
}

// --- Drawing helpers --------------------------------------------------------------

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

void drawDegree(int16_t x, int16_t y, int16_t radius, uint16_t color) {
  tft.drawCircle(x, y, radius, color);
  if (radius >= 3) tft.drawCircle(x, y, radius - 1, color);
}

// --- Status screen -----------------------------------------------------------------

static void leaveCurrent() {
  if (current != NO_THEME) SCREENS[current].leave();
  current = NO_THEME;
}

void showStatus(const char *line1, const char *line2, const char *line3) {
  if (mode != MODE_STATUS || !statusDrawn) {
    leaveCurrent();
    mode = MODE_STATUS;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(FW_NAME, 120, 12, 4);
    tft.drawString("v" FW_VERSION, 120, 42, 2);
    statusDrawn = true;
  }
  themesAt = 0;
  drawFit(line1, 100, TFT_YELLOW);
  drawFit(line2, 130, TFT_WHITE);
  drawFit(line3, 160, TFT_WHITE);
}

// --- Themes ---------------------------------------------------------------------------

static bool hasPicture = false;
static void noteFile(const char *name, size_t, void *) {
  if (media::isJpg(name) || media::isGif(name)) hasPicture = true;
}

// Whether a theme is worth showing in the rotation right now. A manual choice is never
// filtered: the screen explains by itself what is missing.
static bool available(uint8_t theme) {
  switch (theme) {
    case settings::THEME_WEATHER_CLOCK:
    case settings::THEME_FORECAST:
      return settings::hasCity();
    case settings::THEME_ALBUM:
      hasPicture = false;
      media::listDir(config::DIR_IMAGE, noteFile, nullptr);
      return hasPicture;
    default:
      return true;
  }
}

static void switchTo(uint8_t theme) {
  leaveCurrent();
  current = theme;
  themeSince = millis();
  SCREENS[theme].enter();
  needFull = true;  // clear + Update(true) at the next pass
}

// Next theme after `from` that takes part in the rotation and is available; `from`
// itself if it is the only one; the clock if none is.
static uint8_t nextInRotation(uint8_t from) {
  const settings::Settings &s = settings::get();
  for (uint8_t i = 1; i <= settings::THEME_COUNT; i++) {
    uint8_t t = (from == NO_THEME ? i - 1 : from + i) % settings::THEME_COUNT;
    if ((s.autoMask & (1u << t)) && available(t)) return t;
  }
  return settings::THEME_CLOCK;
}

static uint8_t startTheme() {
  const settings::Settings &s = settings::get();
  if (!s.autoSwitch) return s.theme < settings::THEME_COUNT ? s.theme : settings::THEME_CLOCK;
  if ((s.autoMask & (1u << s.theme)) && available(s.theme)) return s.theme;
  return nextInRotation(s.theme);
}

static void enterThemes() {
  mode = MODE_THEMES;
  statusDrawn = false;
  themesAt = 0;
  switchTo(startTheme());
}

void showThemes() { enterThemes(); }

void showThemesAfter(uint32_t ms) {
  if (ms == 0) {
    enterThemes();
    return;
  }
  themesAt = millis() + ms;
  if (themesAt == 0) themesAt = 1;
}

void requestRedraw() { needFull = true; }

uint8_t currentTheme() { return current; }

void settingsChanged(uint32_t changed) {
  applyBacklight();
  if (mode != MODE_THEMES) return;
  const settings::Settings &s = settings::get();
  if (changed & settings::CH_THEME) {
    themeSince = millis();
    uint8_t wanted = startTheme();
    if (!s.autoSwitch) {
      if (wanted != current) switchTo(wanted);
    } else if (current == NO_THEME || !(s.autoMask & (1u << current))) {
      switchTo(wanted);
    }
  }
  if (changed & settings::CH_VISUAL) needFull = true;
}

void loop() {
  uint32_t now = millis();

  if (now - lastBacklightCheck >= 1000) {  // night mode window / clock sync
    lastBacklightCheck = now;
    applyBacklight();
  }

  if (mode == MODE_STATUS) {
    if (themesAt != 0 && (int32_t)(now - themesAt) >= 0) enterThemes();
    return;
  }
  if (current == NO_THEME) return;

  const settings::Settings &s = settings::get();
  if (s.autoSwitch && now - themeSince >= (uint32_t)s.autoInterval * 1000UL) {
    uint8_t next = nextInRotation(current);
    themeSince = now;
    if (next != current) switchTo(next);
  }

  if (needFull) {
    needFull = false;
    tft.fillScreen(TFT_BLACK);
    SCREENS[current].update(true);
  } else {
    SCREENS[current].update(false);
  }
}

}  // namespace display
