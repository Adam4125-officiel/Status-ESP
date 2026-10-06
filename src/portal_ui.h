// What the two Status-Portal screens (screen_portal.cpp, screen_resources.cpp) have in common:
// the colours, the words for a status, the "nothing to show" notices, and two text helpers.
// Everything here reads the portal cache and the Wi-Fi state; nothing touches the network.
#pragma once

#include <Arduino.h>

namespace portal_ui {

// RGB565.
const uint16_t RED = 0xF800;
const uint16_t ORANGE = 0xFD20;
const uint16_t YELLOW = 0xFFE0;
const uint16_t GREEN = 0x07E0;
const uint16_t BLUE = 0x1B3F;         // banner and maintenance
const uint16_t LIGHT_BLUE = 0x5D7F;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GREY = 0xBDF7;
const uint16_t GREY = 0x8410;
const uint16_t TRACK = 0x2104;        // the empty part of a bar

// Colour for a portal::Status (dot, tag, counts); a banner uses the same colour as its background.
uint16_t statusColor(uint8_t status);
// "DOWN", "DEGRADED", "MAINT", "SLOW" ...: what a service row says on its right.
const char *statusTag(uint8_t status);
// Colour for a portal::Severity (bars and percentages).
uint16_t severityColor(uint8_t severity);

// Why there is nothing to show, shared by both screens so that they always agree.
enum Kind : uint8_t {
  NONE = 0,         // a fresh answer: draw it
  NOT_CONFIGURED,   // no address or no key
  NO_NETWORK,       // set up, but the Wi-Fi is down
  LOADING,          // set up, no attempt has finished yet
  UNREACHABLE       // the last attempt failed and there is no fresh answer: the reason is portal::diag().error
};
Kind notice();
// Changes when what drawNotice(kind) would draw changes (the kind, and the reason for UNREACHABLE).
uint32_t noticeKey(Kind kind);
// Draws the notice in the middle of the screen. The caller clears the screen first.
void drawNotice(Kind kind);

// Copies `text` into out, cut with "..." so that it is at most maxW pixels wide in `font`.
void fitText(char *out, size_t cap, const char *text, int16_t maxW, uint8_t font);

// A duration in seconds as short text: "<1m", "12m", "5h20m", "30h", "3d".
void formatSpan(char *out, size_t cap, uint32_t seconds);

}  // namespace portal_ui
