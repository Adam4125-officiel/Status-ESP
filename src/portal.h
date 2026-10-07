// Status-Portal client: asks a Status-Portal (1.10.0 or newer) how the services are doing and caches
// the answer for the two screens (screen_portal.cpp, screen_resources.cpp) and the alert.
//
//   GET http://<portal>/api/device/summary?sections=<the switched-on ones>      X-Api-Key: <key>
//
// The answer is compact JSON of at most 8 KB (portal_data.h has the contract and the parser).
//
// Threading and blocking: there are no threads. portal::loop() runs on every loop pass and returns at
// once unless a request is due. When one is due it blocks for at most ~3 s (the portal is on the LAN:
// connect, headers and each stalled read all share that timeout), once per portal_interval, and after
// a failure the retry gets later and later (30 s, 60 s, ... at most the interval) so a portal that is
// down does not stall the display every few seconds. Never from a request handler: the web interface
// only reads the cache, and the "Test connection" button just asks for a request at the next pass.
//
// Memory: the same rule as the weather. The GIF decoder (~24.5 KB in one block) never coexists with
// the request and the parse (~15 KB at the peak), so the GIF is closed first (the album and the
// weather screen reopen it) and the request is skipped, retried soon, when the heap is too low. It also
// never runs right after a weather request, so the two blocking calls do not stack into one long stall.
#pragma once

#include <Arduino.h>

#include "portal_data.h"

namespace portal {

// Why the portal is (not) answering, for the web interface and the two screens.
// `error` is empty once an attempt has worked.
struct Diag {
  char error[64];        // "bad key (HTTP 401)", "HTTP 404: endpoint off or portal too old", "bad JSON: ...", ...
  int16_t httpCode;      // HTTP status of the last attempt, or a negative HTTPClient error; 0 = none
  uint16_t failures;     // consecutive failed attempts
  uint32_t attemptMs;    // millis() of the last attempt (0 = none since boot / since the settings changed)
};

enum AlertLevel : uint8_t { LEVEL_NONE = 0, LEVEL_DEGRADED, LEVEL_DOWN };

// Called once from setup(), before the network is up. Must not touch the network.
void begin();

// Called on every loop() pass. See "Threading and blocking" above.
void loop();

// An address and a key are both set.
bool configured();

// The cached answer. Never null, never blocks; only meaningful while fresh() is true.
const Summary &data();

// True when there is an answer and it is not older than three intervals (at least three minutes): an
// old answer is not shown as if it were current, the screens say the portal is unreachable instead.
bool fresh();

// millis() of the last good answer (0 = none).
uint32_t updatedAtMs();

// What the last attempt did.
const Diag &diag();

// The portal's own clock, advanced by the time since the answer arrived (seconds since 1970 UTC);
// 0 when it is not known. Ages and countdowns are computed against this, never against the device's
// clock, which may not be synced.
uint32_t nowEpoch();

// down -> LEVEL_DOWN, degraded -> LEVEL_DEGRADED, anything else (operational, slow, maintenance, no
// fresh answer) -> LEVEL_NONE: a planned maintenance or a merely slow service is not an alarm.
AlertLevel alertLevel();

// Fetch again at the next loop() pass, even inside the back-off ("Test connection").
void requestRefresh();

// The Status-Portal settings changed (address, key, switches, ...): forget what was cached for the old
// ones and fetch again at the next pass.
void settingsChanged();

}  // namespace portal
