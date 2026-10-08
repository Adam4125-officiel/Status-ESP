# Status-ESP

Alternative firmware for the **GeekMagic SmallTV-Ultra**, a small connected display
(ESP8266, 240x240 ST7789 screen). It installs **over Wi-Fi, from the device's own update
page**, with no need to open the case or plug in a cable, and you can **go back to the
stock firmware** at any time the same way.

> Status: **1.0.0, the first stable release**: a clock, weather, forecast and photo-album display with a
> web interface to configure it, and a window on [Status-Portal](https://github.com/Adam4125-officiel/Status-Portal),
> a sibling project: the display shows its service status, incidents, maintenance, server resources
> (CPU, memory, GPUs, disks, Jellyfin's activity) and announcements (see [Status-Portal](#status-portal)
> below; Status-Portal 1.11.0 or newer is best). It builds and passes the automated checks; what has and has not
> been tested on a real device is in [CLAUDE.md](CLAUDE.md) and [ROADMAP.md](ROADMAP.md), which also say what is planned.

## Installation
**Only for the SmallTV-Ultra** (ESP8266). Not for the SmallTV, Pro, HelloCubic, ...: they
have a different pinout and memory layout.

1. Pick a release on the [Releases page](https://github.com/Adam4125-officiel/Status-ESP/releases)
   (see [Stable releases and pre-releases](#stable-releases-and-pre-releases) below) and
   download two files: `Status-ESP-<version>.bin` and `checksums.txt`.
2. Verify the download against `checksums.txt`:
   - Linux (GNU coreutils 9 or newer): `cksum -c checksums.txt`
   - Windows (PowerShell): `Get-FileHash Status-ESP-<version>.bin -Algorithm SHA256`, then
     compare the result with the `SHA256` line in `checksums.txt`
     (`-Algorithm MD5` works the same way for the `MD5` line).
3. Open `http://<device-ip>/update` in a browser (the device shows its IP address on screen
   at start-up), choose the `.bin` file and submit. Do not
   unplug the device while it uploads.
4. The device restarts and shows the address of its new web interface.

The Wi-Fi credentials are picked up automatically from the stock firmware. The stock
firmware's images and settings are left untouched.

### Stable releases and pre-releases
- A **stable release** (`vX.Y.Z`) has been tested end to end on a real device. This is
  what you want.
- A **pre-release** (`vX.Y.Z-rc.N`, marked "Pre-release" on GitHub) is a release candidate:
  it builds and passes the automated checks, but it has **not been tested on hardware yet**.
  It is meant for testers. It can be installed and reverted exactly like a stable release,
  but expect rough edges.

Version numbering and the release process are described in
[docs/releasing.md](docs/releasing.md).

## Features
- **Eleven display themes**, chosen in the web interface or rotated automatically (you pick
  which ones take part and how long each stays):
  - *Weather clock*: time, date, sunrise and sunset, current conditions, temperature,
    humidity, wind and pressure, and a small 80x80 animated GIF of your choice;
  - *Simple weather clock*: a big time, the weather icon and the current temperature;
  - *Forecast*: the next three days with icon, highest and lowest temperature;
  - *Photo album*: the JPG and animated GIF files in `/image`, one after the other or one fixed
    picture;
  - *Clock*: a big clock in the colours you choose;
  - *Big digits*: the hour over the minutes in very large digits, with a seconds bar;
  - *Word clock*: the time spelled out in English by lighting words in a grid of letters, to the
    nearest five minutes, with four dots for the minutes in between;
  - *Rings*: three concentric rings (seconds, minutes, hours) filling around the digital time;
  - *Binary clock*: hours, minutes and seconds as columns of dots in binary-coded decimal, with
    the plain time below.
- **Weather** from [Open-Meteo](https://open-meteo.com) (no account or API key): search for
  your city from the web interface; choose the units (km/h, m/s or mph; C or F; hPa, kPa, mmHg
  or inHg) and how often it refreshes.
- **Time** over NTP, with an automatic time zone (the offset of the chosen city, daylight
  saving included) or a manual UTC offset; 12 or 24 hours, three date formats, colon blink,
  colours and font of the clock (Digital, Plain or Large).
- **Pictures**: upload and delete files in `/image` (album, 240x240) and `/gif` (weather
  screen, 80x80) from the web interface. Baseline JPEG and GIF.
- **Night mode** (a lower brightness between two times), brightness slider, backlight
  polarity, and a delay before connecting to Wi-Fi at boot for routers that start slowly.
- **Web interface** at `http://<device-ip>/` or `http://status-esp.local/` (the device answers
  to that name over mDNS once it is on your Wi-Fi), a single page with six
  tabs: Status-Portal, Network, Weather, Time, Pictures and Settings. It needs no
  internet access. The Settings tab also shows the device status (memory, sizes, whether going
  back to the stock firmware is still possible) and has a factory reset and a reboot.
  You can export your settings to a file and import them again, make a **full backup** (one
  `.tar` file with every file on the device and the Wi-Fi network, password included, so keep it
  private) and restore it, and optionally protect the
  interface, the API and `/update` with a password (never enforced in the rescue access
  point, so `/update` always stays reachable; see [docs/recovery.md](docs/recovery.md)).
- If the Wi-Fi connection fails, the device opens an access point named **Status-ESP**
  (open, no password) at `http://192.168.4.1` where you can pick a network or update the
  firmware. It retries the Wi-Fi connection automatically every 5 minutes.
- Firmware update through `/update` (firmware only: the form that would erase the file
  system is hidden).

The device stores its own settings in `/custom.json`. That file name predates the
Status-ESP name and is kept on purpose, so settings saved by earlier versions keep working
after an update. Besides that file, the firmware only ever writes to, or deletes from, the
two picture folders `/image` and `/gif`; the other files of the stock firmware are left alone,
except when you restore a backup you made yourself (which writes back what it contains, and
deletes nothing).

## Status-Portal
The display can show information from a [Status-Portal](https://github.com/Adam4125-officiel/Status-Portal)
server on your local network:
1. Update Status-Portal to **1.11.0 or newer** (1.10.0 works but shows less: no GPUs, latencies, Jellyfin
   band or full service list), open its admin panel, **System → Display device**, enable it and copy the key.
2. In the device's web interface, **Status-Portal** tab: enter the portal's local address with
   `http://` (for example `http://192.0.2.10:5000`; the ESP8266 cannot do `https://`) and the key,
   choose what to show and the alert mode, then **Test connection**.
3. Add the `portal` and `resources` themes to the rotation, or let the alert mode switch to them.

## Going back to the stock firmware
Upload the official GeekMagic `.bin` to `/update`. See [docs/recovery.md](docs/recovery.md).

## Building from source
Requirements: Python 3.10 or newer.

| | Windows (PowerShell) | Linux |
|---|---|---|
| Install PlatformIO (into `.venv`, local to the project) | `.\tools\setup.ps1` | `bash tools/setup.sh` |
| Build and check the size | `.\tools\build.ps1` | `bash tools/build.sh` |
| Build the release assets (`dist/v<version>/`) | `py tools\make_release.py` | `python3 tools/make_release.py` |
| Upload to the device | `.\tools\upload.ps1 -Ip <device-ip>` | `bash tools/upload.sh <device-ip>` |

On Windows, keep the project in a folder with a **short path** (for example
`C:\dev\Status-ESP`): the compiler cannot handle long paths.

The firmware version comes from the [`VERSION`](VERSION) file at the repository root. Pushes
and pull requests are built and validated by GitHub Actions; releases are published with
`tools/release.sh`, not by CI (see [docs/releasing.md](docs/releasing.md)).

## Documentation
- [docs/hardware.md](docs/hardware.md): components, pinout, flash layout, size constraint
- [docs/stock-firmware.md](docs/stock-firmware.md): analysis of the stock firmware (API, files)
- [docs/recovery.md](docs/recovery.md): going back to stock, troubleshooting
- [docs/releasing.md](docs/releasing.md): versioning policy and release procedure
- [ROADMAP.md](ROADMAP.md): planned features and known limitations
- [CLAUDE.md](CLAUDE.md): guide for AI agents working on the project
- [CHANGELOG.md](CHANGELOG.md)

## Disclaimer
This is an unofficial project, not affiliated with GeekMagic. Modifying your device's
firmware is done at your own risk. GeekMagic firmware is not redistributed here.

## License
GNU Affero General Public License v3.0, see [LICENSE](LICENSE).

## Libraries and data
- [ESP8266 Arduino core](https://github.com/esp8266/Arduino) 3.1.2 (through PlatformIO espressif8266 4.2.1)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) 2.5.43
- [ArduinoJson](https://arduinojson.org) 7.4.3
- [TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) 1.1.0, of which only the
  [TJpgDec](https://elm-chan.org/fsw/tjpgd/00index.html) decoder is used
- [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) 2.2.3
- Weather data by [Open-Meteo.com](https://open-meteo.com), licensed
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
