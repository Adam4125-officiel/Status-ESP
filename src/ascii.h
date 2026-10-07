// UTF-8 to printable ASCII, for text the display cannot draw as it is (the built-in TFT_eSPI fonts
// only cover ASCII): city names from the geocoding API, service and incident names from
// Status-Portal. No dependency beyond <Arduino.h>, so a PC test can build it.
#pragma once

#include <stddef.h>

namespace ascii {

// Folds UTF-8 text to printable ASCII (accents removed, ligatures expanded, typographic dashes and
// quotes mapped, control characters and anything that has no ASCII form dropped, trailing spaces
// trimmed). Never writes more than cap bytes and always terminates. The result is never longer
// than the input.
void fold(const char *in, char *out, size_t cap);

}  // namespace ascii
