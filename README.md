# Status-ESP

Alternative firmware for the **GeekMagic SmallTV-Ultra**, a small connected display
(ESP8266, 240x240 ST7789 screen). It installs **over Wi-Fi, from the device's own update
page**, with no need to open the case or plug in a cable, and you can **go back to the
stock firmware** at any time the same way.

> Status: early stage. The groundwork is in place (display, Wi-Fi, web interface,
> updates); the actual display features are still to be written. The long-term goal is to
> show status information from [Status-Portal](https://github.com/Adam4125-officiel/Status-Portal),
> a sibling project. That integration does not exist yet.

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
- Display: title, version, a colour test pattern, Wi-Fi status and the web interface address.
- If the Wi-Fi connection fails, the device opens an access point named **Status-ESP**
  (open, no password) at `http://192.168.4.1` where you can pick a network or update the
  firmware. It retries the Wi-Fi connection automatically every 5 minutes.
- Web interface (hostname `status-esp`): status (memory, sizes, whether going back to the
  stock firmware is still possible), brightness, backlight polarity, network change, reboot.
- Firmware update through `/update` (firmware only: the form that would erase the file
  system is hidden).

The device stores its own settings in `/custom.json`. That file name predates the
Status-ESP name and is kept on purpose, so settings saved by earlier versions keep working
after an update.

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
- [CLAUDE.md](CLAUDE.md): guide for AI agents working on the project
- [CHANGELOG.md](CHANGELOG.md)

## Disclaimer
This is an unofficial project, not affiliated with GeekMagic. Modifying your device's
firmware is done at your own risk. GeekMagic firmware is not redistributed here.

## License
GNU Affero General Public License v3.0, see [LICENSE](LICENSE).

## Libraries
- [ESP8266 Arduino core](https://github.com/esp8266/Arduino) 3.1.2 (through PlatformIO espressif8266 4.2.1)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) 2.5.43
