// What the two weather screens show while there is no weather to show.
//
// One decision, shared, so that the weather clock and the forecast always agree about
// WHY they are empty: no city chosen, no network, still waiting for the first answer,
// or the answer is not coming. Reads the settings, the Wi-Fi state and weather::data();
// never touches the network and never blocks.
#pragma once

#include <Arduino.h>

namespace weather_notice {

enum Kind : uint8_t {
  NONE = 0,       // weather::data().valid: draw the weather
  NO_CITY,        // settings::hasCity() is false
  NO_NETWORK,     // a city is set, nothing cached, Wi-Fi is down
  LOADING,        // a city is set, the first fetch has not answered yet
  UNAVAILABLE     // still nothing after UNAVAILABLE_AFTER_MS of waiting
};

// How long a screen waits for the first data before saying it is not coming.
const uint32_t UNAVAILABLE_AFTER_MS = 45000;

// The current reason (NONE when there is data). Cheap: call it on every Update().
Kind current();

// Draws the notice for `kind` (not NONE) in the middle of the screen, in the band that
// starts at y = 96. The caller clears whatever was there before.
void draw(Kind kind);

}  // namespace weather_notice
