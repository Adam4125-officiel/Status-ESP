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
- The owner's device runs **0.1.0** (validated: display, colours, backlight, Wi-Fi, web
  interface, `/update`). It was built before the rename, so it still identifies itself as
  `Custom-0.1.0`, with the rescue access point `SmallTV-Custom`.
- The repository is at **0.3.0-rc.1**, on branch `0.3.0`: translation of everything to English,
  rename to Status-ESP, the `VERSION` file, and the release tooling. It builds, but it has
  **not been tested on the device**. Installing it changes the device's visible identity
  (rescue access point `Status-ESP`, hostname `status-esp`, `/v.json` reports
  `Status-ESP-<version>`).
- **v0.2.0 is a tag only**: it was never published as a release and never installed.
- The repository is going public. See section 4.
- The CI workflow (`.github/workflows/build.yml`) is new in 0.3.0 and has never run: watch
  the first run after the branch is pushed.
- There are no real features yet (clock, weather, images...): to be defined with the owner.

## 3. Absolute rules (never break these)
1. **Every firmware must keep `/update`**, reachable on normal Wi-Fi **and** in the rescue
   access point. Without it the device cannot be recovered without opening it. Never block
   `loop()` for long (the web server must keep answering).
2. **Never format LittleFS**: keep `cfg.setAutoFormat(false)`, never call
   `LittleFS.format()`, never overwrite or delete the stock files (list in
   `docs/stock-firmware.md`). Our own files use dedicated names (`/custom.json`...).
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
src/main.cpp                 the whole firmware (a single file for now)
tools/version.py             PlatformIO pre-script: reads and validates VERSION, defines FW_VERSION
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

## 11. Architecture of `src/main.cpp`
The firmware name and version are two macros: `FW_NAME` is `"Status-ESP"` and `FW_FULL_NAME`
is `FW_NAME "-" FW_VERSION`, for example `"Status-ESP-0.3.0-rc.1"`. `FW_VERSION` is injected by
`tools/version.py` from `VERSION`. That exact byte string must be in the binary:
`make_release.py` checks for it.

Start-up (`setup`):
1. Mounts LittleFS **without formatting**, loads `/custom.json` (`brt`, `blinv`).
2. Backlight PWM (range 0-1023, 1 kHz), display init, "connecting to Wi-Fi" screen.
3. `startWifi()`: credentials remembered by the SDK, else the stock `/config.json`
   (`{"a":ssid,"p":password}`), else the open access point `Status-ESP` (20 s per attempt).
   The hostname is `status-esp`.
4. `setupWeb()`, then the final screen: the web interface IP, or the rescue-mode instructions.

Loop (`loop`): `server.handleClient()`; in rescue mode with no client for 5 minutes, the
device restarts (new Wi-Fi attempt, useful after a power cut).

HTTP routes:
| Route | Purpose |
|---|---|
| `GET /` | status page + settings |
| `GET /set?brt=0..100` / `?blinv=toggle` | brightness / backlight polarity (saved in `/custom.json`) |
| `GET /wifi`, `POST /wifi` | scan + network choice (stored in the SDK's Wi-Fi area), then restart |
| `GET /update` | our page (firmware only), **declared before** `updater.setup()` because the server takes the first handler that matches |
| `POST /update` | `ESP8266HTTPUpdateServer` handling (`firmware` field) |
| `GET /reboot` | restart |
| `GET /v.json` | `{"m":"SmallTV-Ultra","v":"Status-ESP-<version>"}` (same shape as the stock firmware's) |
| anything else | 404, or a redirect to `http://192.168.4.1/` in rescue mode (captive portal) |

## 12. Conventions
- Text shown on the display is **plain ASCII** (TFT_eSPI's GLCD/Font2/Font4 fonts only cover
  ASCII). Use `drawFit()` for variable-length text.
- The web interface and code comments are in English. PowerShell scripts must stay **pure
  ASCII**: Windows PowerShell 5.1 reads a BOM-less file as ANSI.
- Constant strings use `F("...")` to save RAM (about 80 KB in total, about 43 KB free).
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
- `/custom.json` keeps its name through the Status-ESP rename on purpose (section 5).
  Renaming it, or giving it a different layout without a migration, silently drops the
  owner's settings.

## 14. Ideas for later (to validate with the owner)
- NTP clock, weather (Open-Meteo needs no key), showing images/GIFs from LittleFS.
- Reuse the stock files (images in `/image/`, GIFs in `/gif/`) read-only.
- **Status-Portal integration**: show the status information of
  [Status-Portal](https://github.com/Adam4125-officiel/Status-Portal) on the display. Nothing
  exists yet; the design is to be defined with the owner.
- **On-device auto-updater** reading
  `https://github.com/Adam4125-officiel/Status-ESP/releases/latest/download/version.json`
  (the schema is in `docs/releasing.md`). Points to settle first: the repository must be public
  (agreed); HTTPS on the ESP8266 goes through BearSSL, whose RAM and code-size cost has to fit
  the roughly 43 KB of free RAM and the 520,000-byte limit; the download URL redirects to
  `objects.githubusercontent.com`, so redirects must be followed; verify the MD5 with the
  `Updater` class; never install a pre-release automatically; and `/update` must stay
  reachable whatever the updater does.
- Optional password on `/update` (today it is open to the whole local network, like the
  stock firmware).
- Split `main.cpp` into modules when it grows.
