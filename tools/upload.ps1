# Sends a firmware to the device through its /update page (Wi-Fi, no cable).
# ONLY with the device owner's explicit go-ahead.
#   .\tools\upload.ps1 -Ip <device-ip>        -> the firmware just built
#   .\tools\upload.ps1 -Ip 192.168.4.1        -> device in rescue access-point mode
#   .\tools\upload.ps1 -Ip <ip> -File <geekmagic-firmware.bin>   (back to stock)
# The IP is shown on the device's screen at boot.
# If a password is set in the web interface (never needed in rescue mode), put it in the
# STATUS_ESP_PASSWORD environment variable; the user name is admin.

param(
    [Parameter(Mandatory = $true)][string]$Ip,
    [string]$File = ".pio\build\smalltv-ultra\firmware.bin"
)

Set-Location (Split-Path $PSScriptRoot -Parent)

$bin = Get-Item $File -ErrorAction Stop
$bytes = [IO.File]::ReadAllBytes($bin.FullName)

# Guards: valid ESP8266 image, and a size that fits in the free update space
if ($bytes[0] -ne 0xE9 -and -not $File.EndsWith(".gz")) {
    throw "$File is not an ESP8266 image (first byte 0xE9 expected)."
}
if ($bin.Length -gt 530000) {
    throw "$File is $($bin.Length) bytes: too big for the update space."
}

$before = (Invoke-WebRequest "http://$Ip/v.json" -UseBasicParsing -TimeoutSec 5 -ErrorAction Stop).Content
Write-Host "Before: $before"
Write-Host "Sending $($bin.Name) ($($bin.Length) bytes) to http://$Ip/update ..."

$auth = @()
if ($env:STATUS_ESP_PASSWORD) { $auth = @("--user", "admin:$($env:STATUS_ESP_PASSWORD)") }
curl.exe --fail --silent --show-error @auth -F "firmware=@$($bin.FullName)" "http://$Ip/update"
if ($LASTEXITCODE -ne 0) { throw "Upload failed (curl exit code $LASTEXITCODE)." }

Write-Host "`nThe device is restarting..."
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 3
    try {
        $after = (Invoke-WebRequest "http://$Ip/v.json" -UseBasicParsing -TimeoutSec 3).Content
        Write-Host "After: $after"
        exit 0
    } catch { }
}
Write-Host "The device does not answer at http://$Ip after 90 s. Look at its screen: if it shows" `
    "'No Wi-Fi', connect to the Status-ESP network -> http://192.168.4.1" -ForegroundColor Yellow
exit 1
