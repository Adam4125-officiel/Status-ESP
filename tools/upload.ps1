# Envoie un firmware sur l'ecran via sa page /update (Wi-Fi, sans cable).
#   .\tools\upload.ps1 -Ip <ip-de-l-ecran>      -> firmware compile
#   .\tools\upload.ps1 -Ip 192.168.4.1           -> ecran en point d'acces de secours
#   .\tools\upload.ps1 -Ip <ip> -File <firmware-geekmagic.bin>   (retour a l'origine)
# L'IP est affichee sur l'ecran au demarrage.

param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [string]$File = ".pio\build\smalltv-ultra\firmware.bin"
)

Set-Location (Split-Path $PSScriptRoot -Parent)

$bin = Get-Item $File -ErrorAction Stop
$bytes = [IO.File]::ReadAllBytes($bin.FullName)

# Garde-fous : image ESP8266 valide et taille compatible avec l'espace libre
if ($bytes[0] -ne 0xE9 -and -not $File.EndsWith(".gz")) {
    throw "$File n'est pas une image ESP8266 (premier octet 0xE9 attendu)."
}
if ($bin.Length -gt 530000) {
    throw "$File fait $($bin.Length) octets : trop gros pour l'espace de mise a jour."
}

$before = (Invoke-WebRequest "http://$Ip/v.json" -UseBasicParsing -TimeoutSec 5 -ErrorAction Stop).Content
Write-Host "Avant : $before"
Write-Host "Envoi de $($bin.Name) ($($bin.Length) octets) vers http://$Ip/update ..."

curl.exe --fail --silent --show-error -F "firmware=@$($bin.FullName)" "http://$Ip/update"
if ($LASTEXITCODE -ne 0) { throw "Echec de l'envoi (curl code $LASTEXITCODE)." }

Write-Host "`nRedemarrage de l'ecran..."
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 3
    try {
        $after = (Invoke-WebRequest "http://$Ip/v.json" -UseBasicParsing -TimeoutSec 3).Content
        Write-Host "Apres : $after"
        exit 0
    } catch { }
}
Write-Host "L'ecran ne repond pas a http://$Ip apres 90 s. Regarder l'ecran : s'il affiche" `
    "'Pas de Wi-Fi', se connecter au reseau SmallTV-Custom -> http://192.168.4.1" -ForegroundColor Yellow
exit 1
