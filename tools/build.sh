#!/usr/bin/env bash
# Compile le firmware et verifie qu'il respecte la limite de taille.
# Resultat : .pio/build/smalltv-ultra/firmware.bin
set -euo pipefail
cd "$(dirname "$0")/.."

# Toolchain stockee dans le projet (et pas dans ~/.platformio)
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
.venv/bin/pio run

# Le firmware d'origine ne laisse que ~540 Ko libres pour une mise a jour,
# et notre firmware doit laisser >= 505 200 octets pour un retour a l'origine.
MAX_SIZE=520000
size=$(stat -c %s .pio/build/smalltv-ultra/firmware.bin)
echo "firmware.bin : $size octets (limite $MAX_SIZE)"
if [ "$size" -gt "$MAX_SIZE" ]; then
    echo "TROP GROS : il ne pourra pas etre installe depuis le firmware d'origine." >&2
    exit 1
fi
