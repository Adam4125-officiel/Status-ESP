#!/usr/bin/env bash
# Builds the firmware and checks that it respects the size limit.
# Output: .pio/build/smalltv-ultra/firmware.bin
set -euo pipefail
cd "$(dirname "$0")/.."

# Keep the toolchain inside the project (not in ~/.platformio)
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
.venv/bin/pio run

# The stock firmware leaves only ~540 KB free for an update, and our firmware must
# leave >= 505 200 bytes free so that going back to stock stays possible.
# The image must be strictly smaller than MAX_SIZE.
MAX_SIZE=520000
version=$(tr -d '[:space:]' < VERSION)
size=$(stat -c %s .pio/build/smalltv-ultra/firmware.bin)
echo "firmware.bin: version $version, $size bytes (limit $MAX_SIZE)"
if [ "$size" -ge "$MAX_SIZE" ]; then
    echo "TOO BIG: it could not be installed from the stock firmware." >&2
    exit 1
fi
