// STUB (phase 1): placeholder so the firmware builds. The real fetcher replaces this file.
#include "weather.h"

namespace weather {

static Data cache;

void begin() { memset(&cache, 0, sizeof(cache)); }
void loop() {}
const Data &data() { return cache; }
void requestRefresh() { memset(&cache, 0, sizeof(cache)); }

}  // namespace weather
