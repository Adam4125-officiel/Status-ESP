// A tiny mDNS responder: it answers "what is the address of status-esp.local?" and nothing
// else, so the web interface can be reached without knowing the IP address.
//
// Why not the ESP8266mDNS library: it is a full service-discovery stack and costs about
// 21 KB of flash, a quarter of what is left under the 520,000-byte firmware limit. This is
// ~1.5 KB. What it does NOT do: service discovery (no _http._tcp record, so the device does
// not appear in a network browser), conflict detection (two devices with the same name will
// both answer), IPv6. The name is config::HOSTNAME + ".local".
//
// Station mode only: begin() is called once the Wi-Fi is connected and never in rescue-AP
// mode (the address is always 192.168.4.1 there). loop() is called on every pass: it looks
// for at most one datagram, never blocks.
#pragma once

#include <Arduino.h>

namespace mdns {

// Joins the mDNS multicast group and announces the address. Returns whether it is running.
bool begin();
void loop();
bool active();
// "status-esp.local" while running, "" otherwise.
String name();

}  // namespace mdns
