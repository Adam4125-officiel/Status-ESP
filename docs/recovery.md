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

## The update is refused
- "Not Enough Space": the `.bin` is too big for the free space (see
  [hardware.md](hardware.md#updating-over-wi-fi-the-size-constraint)).
- "Magic Byte": the file is not an ESP8266 firmware (a zip that was not unzipped, a
  corrupted file, or one modified by an antivirus). Verify the checksum against
  `checksums.txt` (see the [README](../README.md#installation)).
