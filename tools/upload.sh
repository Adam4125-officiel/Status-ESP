#!/usr/bin/env bash
# Sends a firmware to the device through its /update page (Wi-Fi, no cable).
# Linux equivalent of tools/upload.ps1.
#
# ONLY RUN THIS WITH THE DEVICE OWNER'S EXPLICIT GO-AHEAD: it replaces the firmware
# that is running on a real device.
#
# Usage: bash tools/upload.sh <device-ip> [firmware.bin]
#   bash tools/upload.sh <device-ip>               -> the firmware just built
#   bash tools/upload.sh 192.168.4.1               -> device in rescue access-point mode
#   bash tools/upload.sh <ip> <geekmagic-firmware.bin>   (back to stock)
# The IP is shown on the device's screen at boot.
# If a password is set in the web interface (never needed in rescue mode), put it in the
# STATUS_ESP_PASSWORD environment variable; the user name is admin.
#
# Guards: the file must start with 0xE9 (ESP8266 image; a .gz file is exempt) and
# be at most 530000 bytes. /v.json is read before the upload (the device must
# answer) and polled for up to 90 s afterwards.
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "upload.sh: $*" >&2; exit 1; }

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0" >&2
    exit 2
fi
ip="$1"
file="${2:-.pio/build/smalltv-ultra/firmware.bin}"

[[ $ip =~ ^[A-Za-z0-9._:-]+$ ]] || die "'$ip' does not look like an IP address or host name"
command -v curl >/dev/null 2>&1 || die "curl is not installed"
[ -f "$file" ] || die "file not found: $file"

# Guards: valid ESP8266 image, and a size that fits in the free update space
size=$(stat -c %s "$file")
first_byte=$(head -c 1 "$file" | od -An -tx1 | tr -d ' \n')
if [ "$first_byte" != "e9" ] && [[ $file != *.gz ]]; then
    die "$file is not an ESP8266 image (first byte 0xE9 expected)."
fi
if [ "$size" -gt 530000 ]; then
    die "$file is $size bytes: too big for the update space."
fi

before=$(curl --silent --show-error --fail --max-time 5 "http://$ip/v.json") \
    || die "the device does not answer at http://$ip/v.json (wrong IP, or not on this network?)"
echo "Before: $before"
echo "Sending $(basename "$file") ($size bytes) to http://$ip/update ..."

# HTTP Basic credentials, only when a password was given (it shows in this machine's process list
# for the duration of the upload).
auth=()
if [ -n "${STATUS_ESP_PASSWORD:-}" ]; then auth=(--user "admin:$STATUS_ESP_PASSWORD"); fi

curl --fail --silent --show-error ${auth[@]+"${auth[@]}"} -F "firmware=@$file" "http://$ip/update" \
    || die "upload failed (curl exit code $?)."

echo
echo "The device is restarting..."
for _ in $(seq 1 30); do
    sleep 3
    if after=$(curl --silent --fail --max-time 3 "http://$ip/v.json" 2>/dev/null); then
        echo "After: $after"
        exit 0
    fi
done
echo "The device does not answer at http://$ip after 90 s. Look at its screen: if it shows" \
    "'No Wi-Fi', connect to the Status-ESP network -> http://192.168.4.1" >&2
exit 1
