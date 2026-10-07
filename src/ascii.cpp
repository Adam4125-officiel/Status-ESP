#include "ascii.h"

#include <Arduino.h>

namespace ascii {

// Code points U+00C0..U+017F, two characters each (' ' = nothing), generated once.
static const char FOLD_TABLE[] PROGMEM =
    "A A A A A A AEC E E E E I I I I D N O O O O O x O U U U U Y Thss"
    "a a a a a a aec e e e e i i i i d n o o o o o / o u u u u y thy "
    "A a A a A a C c C c C c C c D d D d E e E e E e E e E e G g G g "
    "G g G g H h H h I i I i I i I i I i IJijJ j K k k L l L l L l L "
    "l L l N n N n N n n N n O o O o O o OEoeR r R r R r S s S s S s "
    "S s T t T t T t U u U u U u U u U u U u W w Y y Y Z z Z z Z z s ";

void fold(const char *in, char *out, size_t cap) {
  if (cap == 0) return;
  size_t n = 0;
  auto put = [&](char c) {
    if (n + 1 < cap) out[n++] = c;
  };
  while (*in) {
    uint8_t c = (uint8_t)*in++;
    uint32_t cp;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && (*in & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x1F) << 6) | (*in++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && (in[0] & 0xC0) == 0x80 && (in[1] & 0xC0) == 0x80) {
      cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(in[0] & 0x3F) << 6) | (in[1] & 0x3F);
      in += 2;
    } else {
      // 4-byte sequence or garbage: skip the continuation bytes, nothing to draw anyway
      while ((*in & 0xC0) == 0x80) in++;
      continue;
    }
    if (cp >= 32 && cp < 127) {
      put((char)cp);
    } else if (cp >= 0xC0 && cp <= 0x17F) {
      uint16_t i = (uint16_t)(cp - 0xC0) * 2;
      char a = (char)pgm_read_byte(FOLD_TABLE + i);
      char b = (char)pgm_read_byte(FOLD_TABLE + i + 1);
      if (a != ' ') put(a);
      if (b != ' ') put(b);
    } else if (cp == 0x2010 || cp == 0x2011 || cp == 0x2013 || cp == 0x2014) {
      put('-');
    } else if (cp == 0x2018 || cp == 0x2019) {
      put('\'');
    }
    // anything else (Cyrillic, CJK, ...) has no ASCII form: dropped
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

}  // namespace ascii
