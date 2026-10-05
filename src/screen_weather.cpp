// STUB (phase 1): theme weather_clock (clock + today's weather + small GIF).
// The real screen replaces this file; the three functions are the screen contract
// documented in display.h.
#include "display.h"

void screenWeatherEnter() {}
void screenWeatherUpdate(bool full) {
  if (full) display::drawMessage("Weather", "coming in this build");
}
void screenWeatherLeave() {}
