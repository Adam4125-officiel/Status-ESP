// Stand-in for the Arduino core, just enough for the modules that are tested on a PC
// (ascii.cpp, portal_parse.cpp, and the two Status-Portal screens). See tools/test_host.sh.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

// The test sets the clock (test_portal_screens.cpp defines it).
uint32_t millis();

// Just enough of String for the declarations in net.h; nothing here uses it.
class String : public std::string {
 public:
  String() {}
  String(const char *s) : std::string(s) {}
};

#if !(defined(__GLIBC__) && __GLIBC_PREREQ(2, 38))
inline size_t strlcpy(char *dst, const char *src, size_t cap) {
  size_t len = strlen(src);
  if (cap) {
    size_t n = len >= cap ? cap - 1 : len;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return len;
}
inline size_t strlcat(char *dst, const char *src, size_t cap) {
  size_t have = strnlen(dst, cap);
  if (have == cap) return cap + strlen(src);
  return have + strlcpy(dst + have, src, cap - have);
}
#endif
