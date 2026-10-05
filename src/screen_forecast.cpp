// STUB (phase 1): theme forecast (next 3 days). The real screen replaces this file;
// the three functions are the screen contract documented in display.h.
#include "display.h"

void screenForecastEnter() {}
void screenForecastUpdate(bool full) {
  if (full) display::drawMessage("Forecast", "coming in this build");
}
void screenForecastLeave() {}
