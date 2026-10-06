// Screen manager: owns the TFT, the backlight, the current theme and the theme rotation.
//
// ---------------------------------------------------------------------------------
// SCREEN CONTRACT (for screen_*.cpp). A theme is three plain functions:
//
//   void screenXxxEnter();         // the theme is about to become active
//   void screenXxxUpdate(bool full);
//   void screenXxxLeave();         // the theme is being replaced
//
// Enter  : reset the screen's cached state, allocate what it needs. Do NOT draw (the
//          manager clears the screen to black right after Enter and then calls
//          Update(true)). Never block.
// Update : called on EVERY loop() pass while the theme is active (thousands of times a
//          second), so it must return in well under ~20 ms unless it has real work.
//          full == true : first call after Enter, or after a settings change that
//                         affects what is drawn. The screen has just been cleared to
//                         black: repaint everything.
//          full == false: redraw ONLY what changed since the last call (a seconds
//                         digit, a new picture, the next GIF frame). Never clear the
//                         whole screen (flicker). Never touch the network. A JPG
//                         decode (~100-300 ms) is acceptable once per picture.
// Leave  : release resources (GIF decoder, files). May draw nothing. Idempotent.
//
// Names: screenClock*, screenWeather* (theme weather_clock), screenForecast*,
// screenAlbum*, screenAnalog*, screenDigital2*, screenSimpleWeather*. All sets are
// declared below; the manager calls them through its own table, so a screen file only has
// to define its three functions.
//
// Helpers for screens: the global `tft`, display::drawFit / drawMessage / drawDegree,
// timekeeping.h (time, date, colours), units.h and weather.h (data), media.h.
// On-screen text is ASCII only (TFT_eSPI's built-in fonts); the degree sign is drawn
// as a small circle (drawDegree). Fonts loaded: GLCD, 2, 4, 6 (digits), 7 (7-segment), 8 (the narrow "8N" digits, see bigfont.h).
// ---------------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "settings.h"

extern TFT_eSPI tft;

void screenClockEnter();
void screenClockUpdate(bool full);
void screenClockLeave();

void screenWeatherEnter();
void screenWeatherUpdate(bool full);
void screenWeatherLeave();

void screenForecastEnter();
void screenForecastUpdate(bool full);
void screenForecastLeave();

void screenAlbumEnter();
void screenAlbumUpdate(bool full);
void screenAlbumLeave();

void screenAnalogEnter();
void screenAnalogUpdate(bool full);
void screenAnalogLeave();

void screenDigital2Enter();
void screenDigital2Update(bool full);
void screenDigital2Leave();

void screenSimpleWeatherEnter();
void screenSimpleWeatherUpdate(bool full);
void screenSimpleWeatherLeave();

namespace display {

// setup(): backlight PWM, TFT init. Same order as the 0.1.0 firmware.
void begin();
// Every loop() pass: brightness / night mode (once a second), theme rotation, active screen.
void loop();

// Shows a boot / information screen (name, version, up to three lines) instead of any
// theme. Stays until showThemes() / showThemesAfter(). Cheap to call again with new text.
void showStatus(const char *line1, const char *line2 = "", const char *line3 = "");
// Leave the status screen and start the themes now / after ms milliseconds.
// Never called in rescue-AP mode: the instructions stay on screen.
void showThemes();
void showThemesAfter(uint32_t ms);

// After settings::apply(): re-applies brightness / night mode, switches theme if the
// manual choice changed, restarts the rotation timer, and schedules a full repaint when
// anything visible changed. `changed` is the CH_* mask returned by apply().
void settingsChanged(uint32_t changed);
// Forces Update(true) at the next pass (screen is cleared first).
void requestRedraw();

uint8_t currentTheme();   // a settings::Theme, or 255 while a status screen is shown

// Sets the PWM from the current settings (day or night brightness, polarity).
void applyBacklight();

// --- Drawing helpers -------------------------------------------------------------
// Text centred on x = 120 at y (top), Font 4 if it fits in 232 px, Font 2 otherwise.
// Overwrites a 238 px wide band, so a shorter text over a longer one leaves no residue.
void drawFit(const char *text, int16_t y, uint16_t color, uint16_t bg = TFT_BLACK);
// Two centred lines (title in Font 4, detail in Font 2) around the middle of the screen:
// the standard "nothing to show" notice ("Set a city in the web UI").
void drawMessage(const char *title, const char *detail, uint16_t color = TFT_WHITE);
// Small ring used as the degree sign, centred on (x, y).
void drawDegree(int16_t x, int16_t y, int16_t radius, uint16_t color);

}  // namespace display
