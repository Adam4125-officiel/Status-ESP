// Stand-in: the SDK's saved Wi-Fi configuration, as backup.cpp reads it.
#pragma once
#include <stdint.h>

struct station_config {
  uint8_t ssid[32];
  uint8_t password[64];
};

// The test sets what the "SDK" holds.
extern station_config fakeWifiConfig;
extern bool fakeWifiPresent;
inline bool wifi_station_get_config_default(station_config *c) {
  if (!fakeWifiPresent) return false;
  *c = fakeWifiConfig;
  return true;
}
