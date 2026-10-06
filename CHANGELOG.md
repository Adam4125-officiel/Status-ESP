# Changelog

All notable changes to this project are documented in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the versioning policy (what
changes the first, second and third number, and what a `-rc.N` pre-release is) is in
[docs/releasing.md](docs/releasing.md).

## [0.4.0-rc.1] - 2026-10-06

### Added
- **Status-Portal client** (`portal.cpp`). The device asks a Status-Portal 1.10.0 or newer how its
  services are doing (`GET /api/device/summary`, key in the `X-Api-Key` header, plain `http://` on the
  local network: the ESP8266 has no TLS) and keeps the answer for the screens and the alert. It
  asks only for the sections that are switched on, every 30 to 600 seconds (60 by default), checks
  the status code before it reads anything (a 404 or 405 answers with a 2.4 KB HTML page that is
  dropped unread), reads the body through `HTTPClient` into a String capped at 4 KB, and never
  runs while a GIF decoder is alive or right after a weather request. Failures say why in words:
  `bad key (HTTP 401)`, `HTTP 404: endpoint off or portal too old`, `connection failed`, `no answer
  (timeout)`, `bad JSON: IncompleteInput (812 B)`, `answer cut short (700 of 1168 B)`... and the
  retry gets later after each one (30 s, 60 s, ... at most the interval), so a portal that is down
  does not stall the display every few seconds. `/api/status` gains `portal_on`, `portal_ok`,
  `portal_age`, `portal_overall`, `portal_err`, `portal_try_age`, `portal_fails` and `portal_http`,
  like the weather's.
- **Status-Portal settings** (`portal_url`, `portal_key`, `portal_interval`, `portal_alert` and the
  five section switches `portal_services`, `portal_incidents`, `portal_maintenance`,
  `portal_resources`, `portal_announcements`). The address is checked when it is saved, with a
  sentence for each way it can be wrong (`https://` cannot work, no path, no credentials, port 1 to
  65535); a bare `192.0.2.10:5000` gets its `http://`. The key is write-only like the web
  password: `GET /api/settings` only says whether one is set (`portal_key_set`), the export never
  has it and an import never touches it.
- `POST /api/portal/refresh` asks Status-Portal again at the next loop pass (for the "Test
  connection" button).
- **The Status-Portal tab** replaces the "Coming soon" placeholder: portal address, API key
  (a password field that is never filled in again, with a "Remove the key" button), refresh
  interval, the five switches for what to show, what to do when something is wrong (switch to the
  screen, a dot in the corner, or nothing), a **Test connection** button that saves the form, asks
  the portal at once and shows the answer within seconds, and a status line (also in the Status
  table at the bottom of Settings) saying what the portal reports or why it does not answer.
- **Host tests** (`tools/test_host.sh`, run by CI): the answer parser, the address checker and the
  UTF-8 fold are built with the PC's g++ and the address and undefined-behaviour sanitizers, and
  checked against the contract's example, a worst-case 2.7 KB answer made of quotes, backslashes,
  accents and emoji, every truncation of the example, null and missing fields, a newer schema
  version, the portal's HTML 404 page, and 20,000 randomly damaged answers.

### Changed
- The UTF-8 to ASCII fold moved out of `geocode.cpp` into `ascii.cpp`, and the capped String that
  collects an HTTP answer into `http_body.h`, so the weather and Status-Portal clients share them.

## [0.3.0-rc.3] - 2026-10-06

### Fixed
- **The weather never arrived** ("No weather data" for ever, with a city set and the network fine).
  Two bugs, one behind the other. First, `HTTPClient::begin(client, url)` keeps a *clone* of the
  `WiFiClient` it is given and connects that clone; `weather.cpp` read the body from its own,
  never-connected `client`, got zero bytes, and every fetch ended in `EmptyInput` (which was only
  printed on the serial port). Reading through `http.getStreamPtr()` fixed that on a PC, but on
  the real device the second bug showed up at once, thanks to the new diagnostics: a
  hand-written `available()` / `read()` loop saw the transfer end after one or two TCP segments
  (414 or 950 body bytes of about 1,120), so the JSON ended in `IncompleteInput`. The answer is now
  read whole by `HTTPClient` itself (capped at 4 KB) and parsed from memory. Confirmed on the
  device: the weather and the automatic time-zone offset arrive about 8 seconds after boot.

### Added
- **Full backup and restore** (Settings tab). *Download backup* streams one uncompressed `.tar`
  of every file on the device (settings, pictures, GIFs, the stock firmware's own files) plus the
  Wi-Fi network stored in the SDK's flash area (`status-esp-wifi.json`), generated on the fly:
  no buffer is as big as a file and nothing is written to flash. The file holds the Wi-Fi password
  (and the web password if one is set) in clear text, which the page says; the download is
  refused in rescue mode, where the hotspot is open. *Restore backup* takes such a file back:
  it must start with the marker entry `status-esp-backup.json` (any other file is refused before
  anything is written), each file goes to a temporary name and is renamed over its destination
  when complete, names are sanitised (no `..`, no absolute escape), a file that does not fit is
  skipped, nothing is deleted and the file system is never formatted. The Wi-Fi network is stored
  and the device reboots.
- **Three more display themes**, drawn from scratch like the others: *Word clock* (`words`: the
  time spelled out by lighting words in a grid of letters, to the nearest five minutes, with
  four dots for the minutes in between), *Rings* (`rings`: three concentric rings, seconds
  outside, minutes, then hours, filling clockwise in the hour, minute and second colours around
  the digital time, the date and the weekday) and *Binary clock* (`binary`: six columns of dots
  showing HH MM SS in binary-coded decimal, with the plain time and date below). All three
  repaint only what changed (no full-screen clear, no flicker), follow the Time settings
  (colours, 12/24 hour) and can be chosen manually or taken into the automatic rotation
  (`theme`, `auto_themes`). `auto_themes` is stored as a 16-bit mask now, so up to 16 themes fit.
- **Weather diagnostics.** The last failure (HTTP code, network error, "bad JSON: ...",
  "unexpected answer", "heap too low (N B)", "no city", "no Wi-Fi"), the number of failures in a
  row and the age of the last attempt are kept, returned by `/api/status`
  (`weather_err`, `weather_fails`, `weather_http`, `weather_try_age`), shown in the Weather tab
  (with a "Check now" button, `POST /api/weather/refresh`) and on the "No weather data" screen.

### Changed
- **The firmware is 19 KB smaller** (495,056 -> 476,096 bytes before the new features) so that
  the next features still fit under the 520,000-byte limit: the core's forced floating-point
  `printf` / `scanf` (and `strtod`) are no longer linked (`tools/linkflags.py`; the few decimals
  that were printed with `%f` now go through `units::formatFixed()`), and ArduinoJson is built
  with 32-bit floats and integers. With the backup and the three themes added, the firmware is
  489,488 bytes.
- **The default NTP server is `time.cloudflare.com`** (then `pool.ntp.org` and `time.google.com`).
  A custom server is still tried first, followed by `time.cloudflare.com` and `pool.ntp.org`.

## [0.3.0-rc.2] - 2026-10-06

### Added
- **Three more display themes**, all drawn from scratch (nothing is copied from the stock
  firmware): *Analog clock* (a face with hour, minute and second hands and the date below it),
  *Big digits* (the hour stacked over the minutes in 75 px digits, a bar that fills as the
  seconds pass, and the date) and *Simple weather clock* (a big time, the weather icon and
  the current temperature). They are in the theme list and in the auto-switch checkboxes.
  TFT_eSPI's Font 8 (narrow build) is now compiled in for the big digits.
- **Countdown theme**: pick a date (and optionally a time) and a short label in the Time tab; the
  screen shows the whole days left, switches to HH:MM:SS during the last 24 hours and says
  "Reached!" (and how long ago) afterwards. The target is local time as the device's clock shows
  it. A countdown without a date is skipped by the auto-switch.
- **Sunrise and sunset** on the weather clock screen (in the city's own time, 24 or 12 hours
  like the clock), from the same Open-Meteo request. A polar day or night, where Open-Meteo
  reports no time, simply shows no line. The "feels like" line and the three statistics moved
  a few pixels to make room.
- **A "Large" clock font** (Time tab, next to Digital and Plain): TFT_eSPI's Font 8, 75 px digits.
  The Clock theme then shows HH:MM in it with the seconds below and AM/PM beside them
  (HH:MM:SS cannot fit in 240 px at that size). The other themes show it as Digital.
- **`status-esp.local`**: once connected to Wi-Fi the device answers to that name over mDNS, so
  the IP address is not needed. It is shown on the "Connected" screen, in the Network tab and
  in the status block. It is a small responder of our own (about 4.7 KB; the ESP8266mDNS
  library costs about 21 KB, which the size limit cannot afford): it answers address
  questions only, so the device does not show up in a network browser and two devices with the
  same name are not detected. It does not run in rescue mode.
- **Export and import of the settings** (Settings tab): `GET /api/settings/export` downloads the
  same JSON that `/custom.json` holds, and `POST /api/settings/import` restores such a file
  with exactly the validation `POST /api/settings` applies (a file with no recognisable
  setting is refused). The Wi-Fi network and the uploaded pictures are not part of it.
- **Optional password** (Settings tab, off by default): HTTP Basic authentication, user `admin`,
  for the web interface, the API and `POST /update`. The password is stored in
  `/custom.json` and never returned by any GET or by the export (the API only says whether one
  is set). **It is never enforced in rescue access-point mode**, so `/update` always stays
  reachable: if you forget it, switch the router off until the `Status-ESP` hotspot appears,
  connect to it and remove the password (or factory-reset). A factory reset clears it.
  `/v.json` stays open. `tools/upload.sh` and `upload.ps1` read it from
  `STATUS_ESP_PASSWORD`. See `docs/recovery.md`.

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
