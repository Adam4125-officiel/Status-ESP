# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the versioning policy (what
changes the first, second and third number, and what a `-rc.N` pre-release is) is in
[docs/releasing.md](docs/releasing.md).

## [0.3.0-rc.2] - 2026-10-06

### Added
- **Three more display themes**, all drawn from scratch (nothing is copied from the stock
  firmware): *Analog clock* (a face with hour, minute and second hands and the date below it),
  *Big digits* (the hour stacked over the minutes in 75 px digits, a bar that fills as the
  seconds pass, and the date) and *Simple weather clock* (a big time, the weather icon and
  the current temperature). They are in the theme list and in the auto-switch checkboxes.
  TFT_eSPI's Font 8 (narrow build) is now compiled in for the big digits.

## [0.3.0-rc.1] - 2026-10-05

### Added
- `VERSION` file at the repository root as the single source of truth for the firmware
  version, read at build time by `tools/version.py`.
- `version.json` release asset: a machine-readable description of the release (version,
  size, checksums, download URL). Groundwork for a future on-device auto-updater.
- `tools/release.sh`: publishes a release in one command from the development machine,
  with a `--dry-run` mode that does everything locally and nothing remote.
- `tools/upload.sh`: Linux counterpart of `tools/upload.ps1`.
- `.gitignore`, `.gitattributes` and the CI workflow (`.github/workflows/build.yml`), which
  earlier documentation referred to but which were missing from the repository.
- `docs/releasing.md`: versioning policy and release procedure.
- **A new web interface with six tabs** (Status-Portal, Network, Weather, Time, Pictures,
  Settings). It is one gzipped page embedded in the firmware, generated at build time from
  `web/index.html` by `tools/embed_web.py`; it needs no internet access and also works at
  `http://192.168.4.1/` in rescue mode. Every save shows a "Saved" or error message.
  The Status-Portal tab is a placeholder: the integration is planned (see `ROADMAP.md`).
- A JSON API behind it: `/api/status`, `/api/settings` (partial updates, every value
  validated and clamped, applied live), `/api/wifi/scan`, `/api/wifi`, `/api/files`,
  `/api/upload`, `/api/delete`, `/api/geocode`, `/api/reboot` and `/api/factory-reset`.
  The old `/set`, `/wifi`, `/reboot` and `/v.json` still answer.
- **Weather from Open-Meteo** (no API key, plain HTTP): search for a city from the web
  interface, current conditions and the next three days. Units are chosen separately for
  wind (km/h, m/s, mph), temperature (C, F) and pressure (hPa, kPa, mmHg, inHg); the update
  interval is 10 to 180 minutes. Weather icons are drawn with graphics primitives.
- **Time over NTP** (your own server first, then pool.ntp.org, time.google.com and
  time.cloudflare.com). The time zone is either automatic (the UTC offset Open-Meteo reports
  for the chosen city, so daylight saving follows) or a manual offset in 15-minute steps.
  Colours for hours, minutes and seconds, 12 or 24 hours, three date formats, colon blink and
  a "Digital" or "Plain" font.
- **Four display themes**: weather clock (time, date, conditions, temperature, humidity, wind,
  pressure and a small GIF), forecast (next three days), photo album and a big clock. Choose
  one, or let the device rotate through the ones you tick every 5 to 3600 seconds.
- **Photo album** from the stock `/image` folder: baseline JPEGs and animated GIFs, each shown
  for a configurable time, or one fixed picture. Pictures larger than the screen are reduced
  to fit; a picture that cannot be shown is skipped instead of stopping the album.
- **A GIF on the weather screen**, picked from the stock `/gif` folder (80x80 pixels).
- Uploading and deleting files in `/image` and `/gif` from the Pictures and Weather tabs.
  These are the only two folders the firmware ever writes to or deletes from.
- **Night mode**: a second, lower brightness between two times of day (the window may cross
  midnight).
- A delay before connecting to Wi-Fi after boot (0 to 120 s) for routers that start slower
  than the display, and a **factory reset** that deletes only `/custom.json`.

### Changed
- The firmware is no longer a single `src/main.cpp`: it is split into modules (settings, net,
  web, timekeeping, weather, media, display and one file per theme). `main.cpp` only calls them.
- The old status page is replaced by the new web interface. Brightness and backlight polarity
  keep their `brt` and `blinv` keys in `/custom.json`.
- Libraries added, pinned to exact versions: ArduinoJson 7.4.3, TJpg_Decoder 1.1.0 (only its
  `tjpgd` decoder is used, see `CLAUDE.md`) and AnimatedGIF 2.2.3.
- `firmware.bin` grew from 375,504 to 472,048 bytes (limit 520,000).
- Everything is now in English: documentation, code comments, scripts, the device's web
  interface and the on-screen text.
- Renamed to **Status-ESP** (previously "SmallTV-Custom"): the rescue access point is now
  `Status-ESP`, the hostname is `status-esp`, and `/v.json` now reports
  `Status-ESP-<version>`. The target device is still the SmallTV-Ultra, so the PlatformIO
  environment, the build output path and the `/v.json` model field are unchanged.
- Release assets are now the raw `Status-ESP-<version>.bin`, `version.json` and
  `checksums.txt` instead of a zip file.
- CI builds and validates on every push and pull request, but no longer publishes releases.
- The settings file stays `/custom.json`, so brightness and backlight-polarity settings
  saved by earlier versions survive the update.

## [0.2.0] - unreleased
Tagged (`v0.2.0`) but never published as a binary release and never installed on a device.

### Changed
- Removed the "button" code: the SmallTV-Ultra has neither a button nor a touch area.
- Clean `/update` page: firmware only (the "FileSystem" form, which would erase the whole
  file area, is no longer offered).
- Updated the text about going back to the stock firmware.
- Pinned versions: espressif8266 4.2.1 (Arduino core 3.1.2), TFT_eSPI 2.5.43.

### Added
- Tooling: Windows and Linux scripts (setup, build), upload through `/update`, release zip
  builder, CRC check of a `.bin`, analysis of the stock firmware.
- GitHub Actions: build on every push, automatic release on a `vX.Y.Z` tag. (This workflow
  was never actually committed; see 0.3.0-rc.1.)

## [0.1.0] - installed and validated on the device
First working version, installed and validated on a real device (release date not recorded).

### Added
- ST7789 display (SPI mode 3): colours and backlight (inverted GPIO5) validated.
- Wi-Fi: SDK-stored credentials first, then the stock `/config.json`, then a rescue access
  point.
- Web interface: status, brightness, backlight polarity, Wi-Fi network choice, reboot.
- `/update` (ESP8266HTTPUpdateServer), LittleFS mounted without auto-format.
