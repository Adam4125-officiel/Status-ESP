#!/usr/bin/env bash
# Builds and runs the tests that need no device: they compile the pure modules (ascii.cpp,
# portal_parse.cpp, portal_url.cpp) and the two Status-Portal screens (against a recording stand-in for
# the display) with the host's g++, against the same ArduinoJson the firmware uses.
# Run a firmware build once first (bash tools/build.sh): it downloads the libraries.
#
# Usage: bash tools/test_host.sh
set -euo pipefail
cd "$(dirname "$0")/.."

json=.pio/libdeps/smalltv-ultra/ArduinoJson/src
[ -f "$json/ArduinoJson.h" ] || { echo "test_host.sh: ArduinoJson not found in $json: build the firmware once first (bash tools/build.sh)" >&2; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "test_host.sh: g++ is not installed" >&2; exit 1; }

out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

# Same ArduinoJson configuration as platformio.ini, plus the sanitizers: a parser that reads
# something a portal sent must not be able to write out of bounds, and these tests feed it garbage.
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-maybe-uninitialized \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_USE_LONG_LONG=0 \
    -Itests/host -Isrc -isystem "$json" \
    tests/host/test_portal_parse.cpp src/portal_parse.cpp src/portal_url.cpp src/ascii.cpp \
    -o "$out/test_portal_parse"
"$out/test_portal_parse"

# The two Status-Portal screens, against a recording stand-in for the display (tests/host/TFT_eSPI.h).
# -Wno-format-truncation: units.h's formatFixed() is written for the device's small buffers, and the
# compiler cannot know which values reach it.
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-maybe-uninitialized -Wno-format-truncation \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_USE_LONG_LONG=0 -DFW_VERSION='"test"' \
    -Itests/host -Isrc -isystem "$json" \
    tests/host/test_portal_screens.cpp src/screen_portal.cpp src/screen_resources.cpp src/portal_ui.cpp \
    src/portal_parse.cpp src/ascii.cpp \
    -o "$out/test_portal_screens"
"$out/test_portal_screens"

# The hourly forecast: the parser for Open-Meteo's hourly arrays and the theme that draws them.
g++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-maybe-uninitialized -Wno-format-truncation \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_USE_LONG_LONG=0 -DFW_VERSION='"test"' \
    -Itests/host -Isrc -isystem "$json" \
    tests/host/test_hourly.cpp src/screen_hourly.cpp src/weather_hourly.cpp \
    -o "$out/test_hourly"
"$out/test_hourly"
