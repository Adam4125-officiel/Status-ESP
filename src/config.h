// Status-ESP: build-time identity, hardware constants and limits shared by every module.
#pragma once

#include <Arduino.h>

// --- Identity -------------------------------------------------------------
// FW_VERSION comes from the VERSION file, injected at build time by
// tools/version.py (it is a quoted string, e.g. "0.3.0-rc.1").
#ifndef FW_VERSION
#error "FW_VERSION must be defined by tools/version.py (extra_scripts in platformio.ini)"
#endif
#define FW_NAME "Status-ESP"
// make_release.py checks that this exact byte string is present in the binary.
#define FW_FULL_NAME FW_NAME "-" FW_VERSION   // e.g. "Status-ESP-0.3.0-rc.1"

namespace config {

// --- Hardware -------------------------------------------------------------
// The SmallTV-Ultra has no button and no touch area. Never drive GPIO0 / GPIO2
// low at boot: they are the display's DC / RST lines and the ESP8266 boot pins.
constexpr uint8_t PIN_BACKLIGHT = 5;
constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 240;

// --- Network --------------------------------------------------------------
constexpr const char *HOSTNAME = "status-esp";
constexpr const char *AP_SSID = FW_NAME;               // rescue access point, open
constexpr const char *AP_IP = "192.168.4.1";
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;            // per connection attempt
// In access-point mode with nobody connected, reboot to retry the Wi-Fi
// (e.g. after a power cut: the router boots more slowly than the display).
constexpr uint32_t AP_RETRY_MS = 5UL * 60 * 1000;

// --- Firmware / flash -----------------------------------------------------
constexpr uint32_t STOCK_FW_SIZE = 505200;             // stock firmware 9.0.50 and 9.0.51

// --- Files ----------------------------------------------------------------
// Settings file. The name is a leftover from firmware 0.1.0 and is kept on
// purpose: the owner's device already holds a /custom.json written by 0.1.0,
// and renaming it would silently drop the saved brightness / polarity.
// It does not overwrite any stock file.
constexpr const char *SETTINGS_FILE = "/custom.json";
constexpr const char *SETTINGS_TMP = "/custom.tmp";    // written first, then renamed
constexpr const char *STOCK_WIFI_FILE = "/config.json"; // stock firmware's Wi-Fi credentials (read only)
// The only two folders the firmware may write to or delete from (user uploads).
constexpr const char *DIR_IMAGE = "/image";            // album: .jpg .jpeg .gif, 240x240
constexpr const char *DIR_GIF = "/gif";                // weather screen GIFs, 80x80
constexpr size_t MAX_FILE_NAME = 31;                   // LittleFS name limit (LFS_NAME_MAX is 32)
constexpr size_t FS_RESERVE_BYTES = 16 * 1024;         // never fill LittleFS to the brim

// --- Limits ---------------------------------------------------------------
constexpr size_t MAX_JSON_BODY = 2048;                 // largest accepted POST body

}  // namespace config
