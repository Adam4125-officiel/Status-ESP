// Stand-in for the Arduino core, just enough for the modules that are tested on a PC
// (ascii.cpp, portal_parse.cpp). See tools/test_host.sh.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
