// Wi-Fi: boot delay, connection attempts, rescue access point and the async scan.
//
// Everything is a small non-blocking state machine driven by net::loop(), so the main
// loop (and therefore the web server) never waits for the network.
//
// Sequence, same as the 0.1.0 firmware: 1) the credentials remembered by the SDK
// (20 s), 2) the stock firmware's /config.json (20 s), 3) the open rescue access point
// `Status-ESP` (/update and the web interface stay reachable at 192.168.4.1). In rescue
// mode with nobody connected for 5 minutes the device reboots to retry the Wi-Fi.
// The optional boot delay (settings boot_delay) is waited, radio off, before step 1.
#pragma once

#include <Arduino.h>

namespace net {

enum State : uint8_t { DELAY, TRY_SDK, TRY_STOCK, CONNECTED, ACCESS_POINT };

// setup(): starts the state machine (never blocks).
void begin();
// Every loop() pass.
void loop();

State state();
inline bool isConnected() { return state() == CONNECTED; }
inline bool isAp() { return state() == ACCESS_POINT; }
// True while the sequence is still running (DELAY / TRY_SDK / TRY_STOCK).
inline bool isConnecting() { State s = state(); return s == DELAY || s == TRY_SDK || s == TRY_STOCK; }
// Seconds left of the boot delay (0 outside DELAY).
uint32_t delayRemaining();
// Station IP when connected, access-point IP in rescue mode, "" otherwise.
String ip();

// Async scan. scanStart() begins one (returns false if one is already running);
// scanStatus(): -1 running, -2 none started / consumed, >= 0 finished with that many
// networks. After reading the results call scanDone() to free them.
bool scanStart();
int scanStatus();
void scanDone();

// Stores the credentials in the SDK's Wi-Fi area and reboots. Does not return.
void saveCredentialsAndReboot(const String &ssid, const String &pass);

}  // namespace net
