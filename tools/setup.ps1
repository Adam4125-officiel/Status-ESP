# Installe PlatformIO dans un environnement Python local au projet
# (.venv + .pio-core), sans rien installer ailleurs sur le systeme.
# Prerequis : Python 3.10+ (lanceur "py" sous Windows).

$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not (Test-Path ".venv")) {
    py -m venv .venv
    if ($LASTEXITCODE -ne 0) { Write-Host "Echec de creation de .venv (Python installe ?)" -ForegroundColor Red; exit 1 }
}
.\.venv\Scripts\python.exe -m pip install --quiet --upgrade platformio
if ($LASTEXITCODE -ne 0) { Write-Host "Echec de pip install" -ForegroundColor Red; exit 1 }

.\.venv\Scripts\pio.exe --version
Write-Host "OK. Compiler avec : .\tools\build.ps1"
