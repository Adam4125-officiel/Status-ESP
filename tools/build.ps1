# Builds the firmware and checks that it respects the size limit.
# Output: .pio\build\smalltv-ultra\firmware.bin

# No $ErrorActionPreference = "Stop": under PowerShell 5.1, compiler warnings
# (stderr) would become fatal errors. We test $LASTEXITCODE instead.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# Keep the toolchain inside the project (not in %USERPROFILE%\.platformio)
$env:PLATFORMIO_CORE_DIR = Join-Path $root ".pio-core"

.\.venv\Scripts\pio.exe run
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The stock firmware leaves only ~540 KB free for an update, and our firmware must
# leave >= 505 200 bytes free so that going back to stock stays possible.
# The image must be strictly smaller than $MaxSize.
$MaxSize = 520000
$version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
$bin = Get-Item ".pio\build\smalltv-ultra\firmware.bin"
Write-Host ("firmware.bin: version {0}, {1} bytes (limit {2})" -f $version, $bin.Length, $MaxSize)
if ($bin.Length -ge $MaxSize) {
    Write-Host "TOO BIG: it could not be installed from the stock firmware." -ForegroundColor Red
    exit 1
}
