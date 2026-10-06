// Weather icons drawn with TFT_eSPI primitives (never copy GeekMagic assets).
#pragma once

#include <Arduino.h>

namespace icons {

// Draws the icon for a WMO weather code centred on (cx, cy), fitting a size x size
// box (sizes in use: 80 for the weather screen, ~48 for the forecast). Opaque
// drawing only over a black background; must not allocate memory.
void drawWeather(int16_t cx, int16_t cy, int16_t size, uint8_t wmoCode, bool isDay);

// Short ASCII description of a WMO code ("Clear", "Light rain", ...). Never null.
const char *describe(uint8_t wmoCode);

}  // namespace icons
