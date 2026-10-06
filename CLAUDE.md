# CLAUDE.md - guide for AI agents

Read this file fully before acting. **Reply to the owner in French; everything committed to
the repository (code, comments, docs, commit messages, release notes) is in English.** The
language of the conversation and the language of the repository are two separate things.

## 1. The project in brief
Status-ESP is an alternative firmware for a **GeekMagic SmallTV-Ultra**: a small connected
display (ESP8266 + 240x240 ST7789 screen). The owner eventually wants their own firmware
("their own OS") with their own display features, and meant to work together with
[Status-Portal](https://github.com/Adam4125-officiel/Status-Portal), a sibling project. No
integration with it exists yet.

What the repository provides:
- the **source code**;
- **GitHub Releases** whose assets are the prebuilt `Status-ESP-<version>.bin`, `version.json`
  and `checksums.txt`, to be installed **through the device's `/update` web page**, exactly
  like an official GeekMagic update.

What the repository does **not** provide: any wiring or manual serial-flashing procedure. Do
not add one. The owner has a private fallback, outside the repository.

Founding constraints:
- Installation and updates **only over Wi-Fi** (`/update`).
- **Going back to the stock firmware is always possible** through that same page.
- The stock files on the device (images, settings, Wi-Fi) stay untouched.

## 2. Current state (handover)
- The owner's device ran **0.1.0** at first (validated: display, colours, backlight, Wi-Fi, web
  interface, `/update`), then **0.3.0-rc.2**, and has since taken a few rc.3 test builds. It was
  built before the rename, so 0.1.0 identified itself as `Custom-0.1.0`, with the rescue access
  point `SmallTV-Custom`.
- **0.4.0-rc.1 (branch `0.4.0`, from `0.3.0`)** adds the Status-Portal client (`portal.cpp`, contract:
  Status-Portal's `GET /api/device/summary` with `X-Api-Key`, Status-Portal >= 1.10.0), the
  Status-Portal tab, the `portal` and `resources` themes and the alert modes (`switch` / `indicator`
  / `off`). `firmware.bin` is 510,304 bytes, static RAM 41,260: **the flash budget is nearly spent**
  (limit 520,000), so any further feature must first find savings. Verified on the device: the tab
  saves, a bad address gives `connection failed` without a crash, heap stayed ~32 KB. Not verified:
  real portal data (the owner's portal was still on 1.9.1).
- The 0.3.0 line: the repository was at **0.3.0-rc.3**, on branch `0.3.0`. **rc.1 and rc.2 are published** (as
  pre-releases); rc.3 is published by the release step of the session that made it. The
  firmware has a six-tab web interface and JSON API, Open-Meteo weather (with diagnostics), NTP
  time with automatic or manual time zone (default server `time.cloudflare.com`), JPG and
  animated GIF playback from `/image` and `/gif`, night mode, a Wi-Fi boot delay, a factory
  reset, the `status-esp.local` mDNS name, settings export and import, an optional web password,
  a Large clock font, sunrise/sunset on the weather screen, and, new in rc.3, a **full backup
  and restore** of the file system (`backup.cpp`) and three more themes.
  **Eleven themes**, chosen manually or rotated: weather clock (`weather_clock`), simple weather
  clock (`simple_weather`), forecast, photo album, clock, analog clock (`analog`), big digits
  (`digital2`), countdown, word clock (`words`), rings (`rings`) and binary clock (`binary`).
  `firmware.bin` is 489,488 bytes (limit 520,000; the working target for the next feature, the
  Status-Portal client, is to stay under 500,000), static RAM 37,724 of 81,920 bytes.
- **Verified on the real device (rc.3 test builds, 2026-10-06)**: the weather fetch (`weather_ok`
  and the Auto time-zone offset arrive about 8 s after boot), the backup download (a valid tar,
  `Content-Length` exact, 1.8 MB in 15 s, listed and extracted with `tar`), the restore (refusals
  of junk / non-Status-ESP / truncated archives, a crafted archive with unsafe names, an empty
  file, the Wi-Fi entry with the device's own credentials followed by the reboot), saving and
  rotating the new themes through `/api/settings` without a crash, and the web interface loading
  in a headless Chromium with no script error. **Not verified, because nobody can see the
  screen from here**: how the three new themes look on the real display (they were rendered on a
  PC against a mock `TFT_eSPI` and the pictures looked at), and the earlier list: the real SPI
  output of `pushImage`, GIF decoding speed, `status-esp.local` resolving on a real network, and
  the password prompt in a real browser. The Status-Portal tab is a placeholder.
- Installing it changes the device's visible identity (rescue access point `Status-ESP`,
  hostname `status-esp`, `/v.json` reports `Status-ESP-<version>`).
- **v0.2.0 is a tag only**: it was never published as a release and never installed.
- The repository is going public. See section 4.
- The CI workflow (`.github/workflows/build.yml`) is new in 0.3.0: every run so far (the rc.1
  and rc.2 pushes and pull-request builds) was green; look at the run after each push anyway.
- What is **not** done, and the known limitations, are in [ROADMAP.md](ROADMAP.md).

## 3. Absolute rules (never break these)
1. **Every firmware must keep `/update`**, reachable on normal Wi-Fi **and** in the rescue
   access point. Without it the device cannot be recovered without opening it. Never block
   `loop()` for long (the web server must keep answering). **The optional web password is
   therefore never enforced in the rescue access point** (rule 11): forgot it -> switch the
   router off until the open `Status-ESP` hotspot appears -> connect -> remove the password or
   factory-reset. Any change to authentication must keep that way out.
2. **Never format LittleFS**: keep `cfg.setAutoFormat(false)`, never call
   `LittleFS.format()`, never overwrite or delete the stock files (list in
   `docs/stock-firmware.md`). Our own files use dedicated names (`/custom.json`...).
   **The one exception is the two user folders `/image/` and `/gif/`**: the web interface
   uploads files into them and deletes files from them (`/api/upload`, `/api/delete`), and
   that is the only thing the firmware ever writes or deletes outside `/custom.json`. Every
   other stock file (the stock `*.json`, `/config.json`, the `.vlw` fonts...) stays
   untouched, and `/api/delete` answers 403 for any path that is not directly inside those two
   folders. The stock firmware keeps its own pictures there too (for example
   `/image/boot.jpg`), so the owner can delete those from the web interface as well, and the
   album shows them.
   **The one other exception is restoring the owner's own backup** (`POST /api/backup/restore`,
   `backup.cpp`): it writes back every file the archive holds, the stock firmware's included,
   because that is what a backup is for. It only ever *adds or replaces* (nothing is deleted,
   nothing is formatted), refuses any archive that does not start with the
   `status-esp-backup.json` marker, writes each file to `/restore.tmp` and renames it into place,
   sanitises every name (no `..`, no empty component, no component over 31 characters) and skips
   what does not fit. Do not add another route that writes outside these exceptions.
3. **Keep the flash layout** `board_build.ldscript = eagle.flash.4m3m.ld`.
4. **`firmware.bin` < 520,000 bytes** (checked by `build.*` and `make_release.py`). Above that
   it can no longer be installed from the stock firmware (540,672 bytes free), and it must
   leave at least 505,200 bytes free so going back to stock stays possible.
5. **Never upload a firmware to the device without the owner's explicit go-ahead.** Release
   candidates (`-rc.N` pre-releases) are published by the agent itself at the end of a batch
   of work; **a stable release needs the owner's go-ahead**, given after they tested on the
   device. See section 5.
6. Never drive GPIO0 or GPIO2 low at boot (ESP8266 boot pins).
7. Never show the "FileSystem" form on `/update` again (it would erase the whole file area).
8. No personal information in the repository (section 4).
9. Never commit a `.bin` (section 4).
10. **Memory: the GIF decoder is about 24.5 KB in ONE contiguous block, against a heap of a
    few tens of KB.** It exists only while a GIF is on screen and never alongside an HTTP
    fetch. `media::gifOpen()` / `gifOpenCentered()` allocate it with `new` after checking
    `ESP.getMaxFreeBlockSize()` and the heap left for the web server (a refusal is a
    `media::lastError()` message, never a crash); `gifClose()` frees it; `weather::fetchOnce()`
    closes it before it opens a connection and the weather screen reopens it afterwards. Do
    not allocate it statically, do not keep it open off screen, and do not add another network
    call that can run while a GIF plays without closing it first.
11. **The web password (setting `pw`, HTTP Basic, user `admin`) is never enforced in rescue
    access-point mode** (`web.cpp`: `authEnforced()` is `password set && !net::isAp()`), and it
    must keep covering **every** route except `/v.json`, the 404 / captive-portal answers and
    the library's own CORS preflight: a new route goes through `route()` (or calls `guard()` /
    `authorized()` itself, as `/api/upload` does), and `POST /update` follows the same rule through
    `updater.updateCredentials()`. The password is never returned by any GET or by the export
    (`settings::toJson()` only writes it for the file on the device), and an import never
    touches it. Otherwise a forgotten password would brick the device without opening it.

## 4. Privacy (public repository)
- **Commit nothing personal**: name, e-mail, user name, user-profile or home-directory paths,
  Wi-Fi SSID or password, MAC address, the owner's IP, anything about their network, servers
  or other hardware.
- Generic examples only (`<device-ip>`, `<name>`).
- **Never put a `.bin` in the repository**: our binaries go to Releases; GeekMagic's
  (proprietary) firmwares and personal flash images are never published. `.gitignore` excludes
  them; do not work around it.
- Do not reproduce the API-key-looking strings found in the stock firmware. This is a standing
  decision: they are deliberately left out of `docs/stock-firmware.md`.
- Before every commit, read `git diff --cached` to check none of this slipped in.

## 5. Versioning and releases
The full description is in [docs/releasing.md](docs/releasing.md); the essentials:

- **Policy (`vX.Y.Z`)**: **X** is a complete change of system (very rare, not expected);
  **Y** adds features (frequent, especially early on); **Z** is only bug fixes, security fixes
  and performance fixes, with no features.
- **`VERSION`** (repository root) is the single source of truth: one line, no leading `v`,
  `MAJOR.MINOR.PATCH` or `MAJOR.MINOR.PATCH-rc.N`. `tools/version.py` reads it at build time and
  defines `FW_VERSION`. Never hard-code a version elsewhere. The git tag is `v` + `VERSION`.
- **Release candidate `vX.Y.Z-rc.N`**: published by the agent, automatically, when a batch of
  work is done. Marked "Pre-release" on GitHub, **not yet tested on the device**. A later
  candidate of the same version bumps N.
- **Stable `vX.Y.Z`**: only after the owner says a candidate was tested end to end on the
  device and is stable. Then: merge the PR into `main` with a merge commit, bump `VERSION` to
  `X.Y.Z`, update `CHANGELOG.md`, release.
- **Publishing is done with `tools/release.sh`**, on the dev machine: `bash tools/release.sh
  --dry-run` first (builds, makes the assets, prints the notes, does nothing remote), then
  `bash tools/release.sh`. It refuses unless the tree is clean, `VERSION` is valid, the tag is
  new, `HEAD` is pushed, `CHANGELOG.md` has a `## [<version>]` section and, for a stable
  version, the branch is `main`. Do not create release tags by hand.
- **Assets** (made by `tools/make_release.py` into `dist/v<version>/`):
  `Status-ESP-<version>.bin`, `version.json`, `checksums.txt`. No zip.
- **CI builds and validates; it never publishes.** Do not document or rely on CI publishing
  releases.
- **Settings file**: it stays `/custom.json`, although the project is no longer called
  "Custom". The owner's device already has one written by 0.1.0; renaming it would silently
  drop their brightness and backlight-polarity settings. Keep the name.

## 6. Workflow
- **One branch per version being worked on**, named after it (`0.3.0`). Open a **draft pull
  request** into `main` and keep it open until the work is stable.
- **One commit per completed change**, with its tests, docs and `CHANGELOG.md` entry in that
  same commit. Never one giant commit at the end of a session: `git bisect` and `git revert`
  only work at commit granularity, and release notes are written from the history.
- **Merge with a regular merge commit** (`gh pr merge <n> --merge`), **never squash or
  rebase**: it would collapse the separate per-change commits.
- A **docs-only edit** goes straight to `main` when no branch is open; if a branch is open, it
  rides along on that branch. The stable `VERSION` bump is not a docs-only edit: it is the
  last step of promoting a version (section 5).
- **At the end of each batch of work**: build (`bash tools/build.sh`), update `CHANGELOG.md`,
  commit and push, run `bash tools/release.sh --dry-run`, then `bash tools/release.sh` to
  publish the release candidate, then **tell the owner exactly what to test on the device and
  how**. Typical checks after a firmware change: the screen content, `/`, `/update`, `/v.json`,
  that settings (brightness, backlight polarity) survived, the rescue mode if the Wi-Fi code
  was touched, and going back to the stock firmware.
- Before calling anything done: say plainly what was verified and what was not. This
  environment has no real device, so anything that needs the hardware is unverified until the
  owner has tested it.
- Run `tools/build.sh` and the size check before proposing an installation, and re-read the
  rules in section 3.

## 7. Development environment
- The dev machine is a **Linux VM shared with other projects**. Keep everything
  **project-local**: the Python virtual environment `.venv` and the PlatformIO toolchain
  `.pio-core` live inside the repository (both git-ignored) and are created by
  `tools/setup.sh`. Do not install things system-wide unless it is really needed, and **never
  touch other projects' directories**.
- The GitHub CLI (`gh`) is used for releases and pull requests.
- A VM with no access to the device's local network can build and prepare the release but
  cannot install it: the owner uploads the `.bin` themselves (or gives the go-ahead for
  `tools/upload.sh` when the device is reachable).

## 8. Repository layout
```
VERSION                      single source of truth for the version (one line, no leading "v")
platformio.ini               build (pinned versions) + TFT_eSPI display config (build_flags)
src/main.cpp                 setup() / loop() and the boot screens; everything else is a module
src/config.h                 FW_NAME / FW_FULL_NAME, pins, folder names, limits
src/settings.{h,cpp}         every setting, /custom.json, validation (the same JSON as /api/settings)
src/net.{h,cpp}              Wi-Fi state machine: boot delay, connection attempts, rescue AP, async scan
src/web.{h,cpp}              HTTP routes, the JSON API, file upload / delete, /update page
src/timekeeping.{h,cpp}      SNTP (UTC) + the UTC offset applied by us, date formatting, night window
src/weather.{h,cpp}          Open-Meteo fetch (streamed JSON), cache, back-off
src/geocode.{h,cpp}          city search through Open-Meteo's geocoding API (explicit user action)
src/weather_notice.{h,cpp}   why a weather screen is empty (no city / no network / loading / unavailable)
src/units.h                  unit conversion for display (data is always metric)
src/backup.{h,cpp}           full file-system backup / restore as a tar stream (see section 11)
src/media.{h,cpp}            JPG (tjpgd) and animated GIF (AnimatedGIF) from LittleFS to the screen
src/icons.{h,cpp}            weather icons drawn with graphics primitives, WMO code descriptions
src/display.{h,cpp}          screen manager: themes, rotation, backlight / night mode, drawing helpers
src/screen_*.cpp             one theme each: clock, weather (weather_clock), simple_weather, forecast,
                             album, analog, digital2 (big digits), countdown, words, rings, binary
src/bigfont.h                helpers for the Font 8 (75 px digits) themes
src/countdown_calc.h         date arithmetic for the countdown (pure, testable on a PC)
src/mdns.{h,cpp}, mdns_dns.* the tiny mDNS responder for status-esp.local (address questions only)
src/generated/               web_index.h, built from web/index.html (git-ignored)
web/index.html               the whole web interface: one page, six tabs, vanilla JS
tools/version.py             PlatformIO pre-script: reads and validates VERSION, defines FW_VERSION
tools/embed_web.py           PlatformIO pre-script: gzips web/index.html into src/generated/web_index.h
tools/setup.ps1 | setup.sh   create .venv with PlatformIO (project-local)
tools/build.ps1 | build.sh   build + enforce the size limit
tools/make_release.py        makes dist/v<version>/ : Status-ESP-<version>.bin, version.json, checksums.txt
tools/release.sh             Linux: build + assets + tag + GitHub release (--dry-run supported)
tools/upload.ps1 | upload.sh upload a .bin through /update, checking /v.json before and after
tools/check_firmware.py      checks the Arduino CRC of a .bin (and extracts a firmware from a flash image)
tools/analyze_firmware.py    analyses a firmware binary (segments, gzip pages, strings)
docs/hardware.md             hardware, pinout, flash layout, size constraint
docs/stock-firmware.md       API and files of the stock firmware
docs/recovery.md             going back to stock, rescue Wi-Fi, update errors
docs/releasing.md            versioning policy and release procedure
.github/workflows/build.yml  CI: build + validate on every push and PR (does not publish)
CHANGELOG.md                 Keep a Changelog format, newest first
ROADMAP.md                   what is NOT done: planned features and known limitations
.gitignore | .gitattributes  keep .bin and the local toolchain out; keep .sh/.py on LF endings
```

## 9. Commands
Windows (PowerShell 5.1):
```powershell
.\tools\setup.ps1                 # once
.\tools\build.ps1                 # -> .pio\build\smalltv-ultra\firmware.bin
py tools\make_release.py          # -> dist\v<version>\
.\tools\upload.ps1 -Ip <device-ip>   # installs (with the owner's go-ahead!)
```
Linux:
```bash
bash tools/setup.sh && bash tools/build.sh && python3 tools/make_release.py
bash tools/release.sh --dry-run   # then: bash tools/release.sh
bash tools/upload.sh <device-ip>  # with the owner's go-ahead!
# or: curl -F "firmware=@.pio/build/smalltv-ultra/firmware.bin" http://<device-ip>/update
```
- The toolchain lives in `.pio-core` (`PLATFORMIO_CORE_DIR`, set by the build scripts).
- Version currently on the device: `GET http://<device-ip>/v.json`.
- The device's rescue mode: network `Status-ESP`, IP `192.168.4.1`.

## 10. Hardware (summary; details in docs/hardware.md)
| Item | Value | Status |
|---|---|---|
| SoC / flash | ESP8266EX 26 MHz, 4 MB DIO | verified |
| Display | ST7789 240x240, SPI **mode 3**, 40 MHz; MOSI 13, SCLK 14, DC 0, RST 2, CS not wired | verified |
| Colours | RGB order is correct, no inversion to add | verified |
| Backlight | GPIO5, **inverted** PWM (`blInverted = true` by default) | verified |
| Button / touch | **none**: do not assume one (that mistake was already made once) | verified |
| Other GPIOs | unknown | assume nothing |

The device can therefore only be driven over the network (web interface, API, data fetched
online).

## 11. Architecture (`src/`)
The firmware name and version are two macros in `config.h`: `FW_NAME` is `"Status-ESP"` and
`FW_FULL_NAME` is `FW_NAME "-" FW_VERSION`, for example `"Status-ESP-0.3.0-rc.1"`. `FW_VERSION`
is injected by `tools/version.py` from `VERSION`. That exact byte string must be in the binary:
`make_release.py` checks for it.

There are **no threads and no blocking waits**: every module is a small state machine that
`loop()` calls once per pass, so the web server (and with it `/update`) is served every pass.
The only deliberate blocking calls are the weather fetch (at most ~5 s, once per interval, with
a 60 s back-off after a failure), the city search (explicit user action, 5 s), a JPG decode
(100-300 ms, once per picture), the backup download (explicit action: it streams the whole file
system to one client, 15 s for 1.8 MB, and the screen and every other request wait meanwhile) and
a restore upload (same, `yield()` between chunks).

Start-up (`setup`): serial; `settings::begin()` mounts LittleFS **without formatting** and
loads `/custom.json`; `display::begin()` (backlight PWM, TFT); `weather::begin()`;
`net::begin()` (boot delay, then SDK credentials, then the stock `/config.json`, then the open
rescue access point `Status-ESP`, 20 s per attempt, hostname `status-esp`); `web::begin()`;
"connecting" screen.
Loop order: `web::loop()`, `net::loop()`, the boot screen, `timekeeping::loop()`,
`weather::loop()`, `display::loop()`. In rescue mode with no client for 5 minutes the device
restarts to retry the Wi-Fi.

Settings: one struct in RAM, loaded from and saved to `/custom.json` (ArduinoJson). The file
and `GET/POST /api/settings` use the same JSON shape, validated and clamped in
`settings::apply()`, which returns a `CH_*` mask of what changed; `display::settingsChanged()`
turns that into a live repaint. Keys `brt` and `blinv` are the ones written by 0.1.0 and must
not change. Writes happen only when the user saves, never on a timer.

Screens: the contract is written at the top of `display.h`. A theme is `Enter` / `Update(full)` /
`Leave`; `Update(false)` runs on every loop pass and must redraw only what changed (no
full-screen clear, no flicker), `Update(true)` follows a clear. On-screen text is ASCII; the
degree sign is drawn as a small circle (`display::drawDegree`). The weather screens share
`weather_notice` for the "no city / no network / loading" messages.

Weather: `weather.cpp` fetches `http://api.open-meteo.com/v1/forecast` over plain HTTP (the
device has no TLS), collects the ~1.1 KB answer through `http.writeToPrint()` into a String
capped at 4 KB, parses it through an ArduinoJson filter, and keeps one
metric `Data` struct (`units.h` converts at draw time, so a unit change needs no new fetch).
`utc_offset_seconds` from the same answer is what the clock uses when the time zone is Auto.
The last failure, the failure count and the attempt age are kept (`weather::diag()`) and shown by
`/api/status`, the Weather tab and the "No weather data" screen: read them first when the weather is
missing.

Backup: `backup.cpp` writes a ustar tar on the fly (`writeTar()`, 1 KB buffer, size known beforehand
by `tarSize()`): first the virtual marker `status-esp-backup.json`, then the virtual
`status-esp-wifi.json` (the SSID and password stored in the SDK's flash area, read with
`wifi_station_get_config_default()`; named so because the stock firmware has a real `/wifi.json`),
then every file. The archive therefore holds the Wi-Fi password, and the web password when one is
set: the download is refused in rescue mode and the web interface says so. `restoreFeed()` is a
state machine fed chunk by chunk; it needs one 512-byte header block at a time. A restore that
wrote at least one file or a Wi-Fi network ends in a reboot (the settings in RAM are older than
the files).

Media: JPG goes through `jd_prepare()` / `jd_decomp()` (the `tjpgd` core inside the
TJpg_Decoder library) with a work area allocated only for the decode; GIF goes through
AnimatedGIF, one frame per `gifPlayFrame()` call, honouring the frame delays. Rule 10 in
section 3 is the memory rule. `drawJpgFit()` and `gifOpenCentered()` are what the album uses to
fit and centre pictures that are not exactly 240x240.

HTTP routes:
| Route | Purpose |
|---|---|
| `GET /` | the web interface (one gzipped page from PROGMEM) |
| `GET /api/status`, `GET/POST /api/settings` | status block; every setting, partial updates, applied live |
| `GET /api/wifi/scan`, `POST /api/wifi` | async scan; store the network in the SDK's Wi-Fi area, then restart |
| `GET /api/files?dir=`, `POST /api/upload?dir=`, `POST /api/delete` | list, upload, delete: only `/image` and `/gif` |
| `GET /api/geocode?q=` | city search (top 5) through Open-Meteo |
| `POST /api/weather/refresh` | fetch the weather again at the next pass (the Weather tab's "Check now") |
| `GET /api/settings/export`, `POST /api/settings/import` | the settings as a file, and back (never the password) |
| `GET /api/backup` | every LittleFS file + the Wi-Fi network as one tar; 403 in rescue mode (open hotspot) |
| `POST /api/backup/restore` | multipart upload of such a tar, then reboot; checks the password itself, like `/api/upload` |
| `POST /api/reboot`, `POST /api/factory-reset` | restart; delete `/custom.json` only, then restart |
| `GET /update` | our page (firmware only), **declared before** `updater.setup()` because the server takes the first handler that matches |
| `POST /update` | `ESP8266HTTPUpdateServer` handling (`firmware` field) |
| `GET /set?brt=&blinv=`, `/wifi`, `/reboot` | the 0.1.0 routes, kept for compatibility |
| `GET /v.json` | `{"m":"SmallTV-Ultra","v":"Status-ESP-<version>"}` (same shape as the stock firmware's) |
| anything else | 404, or a redirect to `http://192.168.4.1/` in rescue mode (captive portal) |

## 12. Conventions
- Text shown on the display is **plain ASCII** (TFT_eSPI's built-in fonts only cover ASCII;
  the build loads GLCD, Font 2, 4, 6, 7 and 8 (the narrow "8N" build), and 6, 7 and 8 are digits only). Use `drawFit()` for
  variable-length text and draw the degree sign with `drawDegree()`.
- The web interface and code comments are in English. PowerShell scripts must stay **pure
  ASCII**: Windows PowerShell 5.1 reads a BOM-less file as ANSI.
- Constant strings use `F("...")` to save RAM (80 KB in total; the build's static use is about
  35 KB). The free heap and the largest free block are in the web interface's status block
  (`heap`, `max_block`): read them on the device after any change that allocates, and with a
  GIF playing, since that is the worst case (rule 10).
- Keep the versions pinned in `platformio.ini`; a platform or library update is a change to
  test on the device and to record in the changelog.
- Everything committed is in English. Replies to the owner are in French.

## 13. Known pitfalls
- The classic XOR checksum of ESP8266 images does not match on Arduino binaries: `elf2bin.py`
  writes the size and a CRC at `0x1010`/`0x1014`. Use `tools/check_firmware.py`.
- `ESP8266HTTPUpdateServer`'s `/update` page shows a "FileSystem" form by default: hence our
  custom GET page.
- `WiFi.scanNetworks()` in access-point mode temporarily switches on station mode.
- On the stock firmware, `/config.json` over HTTP masks the password (`"p":"****"`), but the
  file in LittleFS holds it in clear text.
- Uploading files through the github.com web interface does not apply `.gitignore`.
- **Windows: a short project path is mandatory.** The Xtensa compiler cannot handle long
  paths (`fatal error: bits/c++config.h: No such file or directory`). Do not replace `.venv`
  or `.pio-core` with junctions either.
- PowerShell 5.1: do not use `$ErrorActionPreference = "Stop"` around native commands (pio,
  pip, curl): their stderr becomes a fatal error when the output is captured. Test
  `$LASTEXITCODE`. No `&&`: use `;` and `if ($?) { ... }`. Keep scripts pure ASCII.
- `.sh` scripts must keep LF line endings (`.gitattributes`); run them with `bash
  tools/xxx.sh` (the executable bit may be missing).
- The version is read from `VERSION` by `tools/version.py`. A hard-coded `-D FW_VERSION=...`
  in `platformio.ini` would silently fight it: do not add one.
- GitHub's "latest" release only ever resolves to a **stable** release, so
  `releases/latest/download/version.json` never points at a pre-release. That is intended.
- `cksum -c checksums.txt` needs GNU coreutils 9 or newer; older systems can compare by hand
  with `sha256sum`.
- **Do not use the `TJpg_Decoder` class (`TJpgDec`, `drawFsJpg()`...).** Its `User_Config.h`
  defines `TJPGD_LOAD_SD_LIBRARY` unconditionally and its file functions default to `SPIFFS`,
  so using it links the SD library, SdFat and the whole SPIFFS implementation: firmware.bin
  went from 456 KB to 530 KB, over the limit, and the class keeps a 3.5 KB work area in RAM for
  ever. `media.cpp` calls `jd_prepare()` / `jd_decomp()` from `tjpgd.h` directly instead. If you
  ever think of going back to the wrapper, check the size first (`nm --size-sort` on
  `firmware.elf` shows `spiffs_*` and `SDFS` when it has crept back in).
- `pushImage()` needs `tft.setSwapBytes(true)` for the native-endian RGB565 that tjpgd and
  AnimatedGIF produce (the wire wants the high byte first); `media.cpp` sets it before every
  push. Nothing else in the firmware uses `pushImage()`.
- The ESP8266 stack is small (4 KB): keep pixel rows and decoder state in the heap objects
  that own them (the GIF `Player` holds its own row buffer), not in locals.
- `web/index.html` is only compiled in through `tools/embed_web.py`, which writes the
  git-ignored `src/generated/web_index.h`. A build from a clean checkout makes it; do not
  commit the generated file.
- `/image` is shared with the stock firmware, which keeps pictures there (for example
  `/image/boot.jpg`): they appear in the album, and the web interface can delete them.
- **Do not read the weather answer with a hand-written `available()` / `read()` loop over
  `http.getStreamPtr()`.** On the real device that saw the transfer end after one or two TCP
  segments (414 or 950 of ~1120 body bytes, `IncompleteInput`), while `http.writeToPrint()` /
  `getString()` got all of it with the same request. The cause was never found; the first
  version of the fetch also read from the *unconnected* local `WiFiClient` (`HTTPClient::begin`
  keeps a clone of the client it is given), which is why the stream is always `http.getStreamPtr()`
  and never the `client` variable. Look at `weather::diag()` before blaming the network.
- `/custom.json` keeps its name through the Status-ESP rename on purpose (section 5).
  Renaming it, or giving it a different layout without a migration, silently drops the
  owner's settings.

## 14. What is left
Planned features and known limitations are in [ROADMAP.md](ROADMAP.md): smooth
fonts, the Status-Portal integration (the Status-Portal tab is only a placeholder), the
on-device auto-updater (blocked by the size of HTTPS/BearSSL), and so on. Add an idea there, not
here; when something ships, remove it from `ROADMAP.md` (the changelog is the record of what
exists). Check every idea against the 520,000-byte limit and the memory rule first.
