# Installs PlatformIO into a Python environment local to the project
# (.venv; the toolchain itself goes to .pio-core on the first build),
# without installing anything elsewhere on the system.
# Prerequisite: Python 3.10+ (the "py" launcher on Windows).

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not (Test-Path ".venv")) {
    py -m venv .venv
    if ($LASTEXITCODE -ne 0) { Write-Host "Could not create .venv (is Python installed?)" -ForegroundColor Red; exit 1 }
}
.\.venv\Scripts\python.exe -m pip install --quiet --upgrade platformio
if ($LASTEXITCODE -ne 0) { Write-Host "pip install failed" -ForegroundColor Red; exit 1 }

.\.venv\Scripts\pio.exe --version
Write-Host "OK. Build with: .\tools\build.ps1"
