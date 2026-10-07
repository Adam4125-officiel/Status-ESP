# Going back to stock, and troubleshooting

Everything is done over Wi-Fi, from a web browser.

## Going back to the GeekMagic firmware
1. Download the official firmware from the
   [GeekMagic repository](https://github.com/GeekMagicClock/smalltv-ultra) (`Ultra-V...`
   folder), unzip it and check the MD5 that comes with it.
2. Open `http://<device-ip>/update` (the IP is shown on the screen at start-up).
3. Upload the GeekMagic `.bin`. The device restarts on the stock firmware.

The stock images, settings and Wi-Fi credentials are intact: this firmware never formats
the file area.

To check a `.bin` before uploading it: `python tools/check_firmware.py <file.bin>`.

## The screen shows "No Wi-Fi"
The device could not join a network, so the firmware has opened the **Status-ESP** access
point (no password). Connect to it, then:
- `http://192.168.4.1/wifi`: choose a new Wi-Fi network;
- `http://192.168.4.1/update`: install another firmware or go back to stock.

If nobody is connected to it for 5 minutes, the device restarts and tries the Wi-Fi again
(useful after a power cut, when the router takes longer to come back than the device).

## I forgot the web interface password
The password (Settings tab, off by default; user name `admin`) protects the web interface, the
API and firmware updates, but **never the rescue access point**, so there is always a way in:
1. Switch your router off, or take the device out of its range. After its Wi-Fi attempts fail
   (about 40 seconds after power-up) the device opens the open **Status-ESP** access point.
2. Connect to it and open `http://192.168.4.1`: no password is asked there. In the Settings
   tab choose "Remove the password" (or do a factory reset, which also clears it).
3. Switch the router back on. The device retries the Wi-Fi by itself 5 minutes after nobody
   is connected to its access point (or reboot it from the Settings tab).

A firmware update from `/update` on the rescue access point never needs the password either.
The password is stored in clear text in `/custom.json` on the device and is never returned by
the API or included in a settings export. It travels in clear text over plain HTTP like
everything else here (Basic authentication), so it keeps casual visitors out, not someone who
can listen to your network; there is no limit on wrong guesses.

If a password is set and you upload with `tools/upload.sh` or `tools/upload.ps1`, put it in the
`STATUS_ESP_PASSWORD` environment variable (`curl -u admin:<password> -F firmware=@file.bin
http://<device-ip>/update` does the same by hand).

## The update is refused
- "Not Enough Space": the `.bin` is too big for the free space (see
  [hardware.md](hardware.md#updating-over-wi-fi-the-size-constraint)).
- "Magic Byte": the file is not an ESP8266 firmware (a zip that was not unzipped, a
  corrupted file, or one modified by an antivirus). Verify the checksum against
  `checksums.txt` (see the [README](../README.md#installation)).
