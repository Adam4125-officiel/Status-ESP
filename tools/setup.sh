#!/usr/bin/env bash
# Installs PlatformIO into a Python environment local to the project
# (.venv; the toolchain itself goes to .pio-core on the first build).
# Prerequisite: python3 with the venv module.
set -euo pipefail
cd "$(dirname "$0")/.."

[ -d .venv ] || python3 -m venv .venv
.venv/bin/python -m pip install --quiet --upgrade platformio
.venv/bin/pio --version
echo "OK. Build with: bash tools/build.sh"
