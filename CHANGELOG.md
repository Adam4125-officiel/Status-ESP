# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the versioning policy (what
changes the first, second and third number, and what a `-rc.N` pre-release is) is in
[docs/releasing.md](docs/releasing.md).

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

### Changed
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
