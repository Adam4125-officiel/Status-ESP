// STUB (phase 1): placeholder so the firmware builds. The real icons replace this file.
#include "icons.h"

#include "display.h"

namespace icons {

void drawWeather(int16_t cx, int16_t cy, int16_t size, uint8_t wmoCode, bool isDay) {
  (void)wmoCode;
  tft.fillCircle(cx, cy, size / 3, isDay ? TFT_YELLOW : TFT_LIGHTGREY);
}

const char *describe(uint8_t wmoCode) {
  (void)wmoCode;
  return "Weather";
}

}  // namespace icons
