# Compile le firmware et verifie qu'il respecte la limite de taille.
# Resultat : .pio\build\smalltv-ultra\firmware.bin

# Pas de $ErrorActionPreference = "Stop" : sous PowerShell 5.1, les avertissements
# du compilateur (stderr) deviendraient des erreurs fatales. On teste $LASTEXITCODE.
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# Toolchain stockee dans le projet (et pas dans %USERPROFILE%\.platformio)
$env:PLATFORMIO_CORE_DIR = Join-Path $root ".pio-core"

.\.venv\Scripts\pio.exe run
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Le firmware d'origine ne laisse que ~540 Ko libres pour une mise a jour,
# et notre firmware doit laisser >= 505 200 octets pour un retour a l'origine.
$MaxSize = 520000
$bin = Get-Item ".pio\build\smalltv-ultra\firmware.bin"
Write-Host ("firmware.bin : {0} octets (limite {1})" -f $bin.Length, $MaxSize)
if ($bin.Length -gt $MaxSize) {
    Write-Host "TROP GROS : il ne pourra pas etre installe depuis le firmware d'origine." -ForegroundColor Red
    exit 1
}
