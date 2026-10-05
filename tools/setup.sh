#!/usr/bin/env bash
# Installe PlatformIO dans un environnement Python local au projet (.venv + .pio-core).
# Prerequis : python3 avec le module venv.
set -euo pipefail
cd "$(dirname "$0")/.."

[ -d .venv ] || python3 -m venv .venv
.venv/bin/python -m pip install --quiet --upgrade platformio
.venv/bin/pio --version
echo "OK. Compiler avec : tools/build.sh"
