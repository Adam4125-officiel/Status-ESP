# Roadmap

Features planned for later versions. Nothing here exists yet; each item is an idea to design with the
project owner before it is built. When an item ships, remove it from this file (the CHANGELOG is the
record of what exists).

Version numbers follow [docs/releasing.md](docs/releasing.md): new features bump the minor number.

## Display
- **Smooth (anti-aliased) fonts** loaded from LittleFS, as more clock fonts. The built-in
  TFT_eSPI fonts are all the firmware has today (the "Large" font is Font 8).
- **Richer weather**: hourly forecast and more forecast days.

## Status-Portal integration
- Show the status of the services monitored by
  [Status-Portal](https://github.com/Adam4125-officiel/Status-Portal) on the display: overall status,
  services that are down, open incidents. Status-Portal already exposes a public JSON API (`/api/status`).
- Fill the "Status-Portal" tab of the web interface (portal address, refresh interval, which services
  to show, alert colours).

## Updates
- **On-device auto-updater**: the device reads
  `https://github.com/Adam4125-officiel/Status-ESP/releases/latest/download/version.json` (stable releases
  only; schema in [docs/releasing.md](docs/releasing.md)), and offers or installs the new `.bin`.
  **Blocked by size today**: GitHub only serves HTTPS, which needs BearSSL, and BearSSL does not fit
  in what is left. 0.3.0-rc.2 is 495,056 bytes against the 520,000-byte limit (about 25 KB of
  flash left), and the TLS code plus its buffers would cost more than that in flash and a large
  contiguous block of the little heap that is left (see the GIF memory note below). Settle this
  first, then: following the GitHub redirect to the download host, MD5 check with the `Updater`
  class, never installing a pre-release automatically, and `/update` must stay reachable
  whatever the updater does.

## Settings
- Import of the stock firmware's settings (city, units, time format...) on first boot, once their file
  formats are known.

## Known limitations of 0.3.0
These are not planned features but things that are missing or rough today. Fix them when they
get in the way; remove a line when it is no longer true.

- **Nothing has been tested on a device yet** (0.3.0-rc.1 and rc.2 included). The code was built, and its drawing, GIF/JPG
  decoding and album logic were run on a PC against mock hardware; the real screen, the real
  heap, the network calls and the web interface in a browser still have to be checked.
- **Memory while a GIF plays is tight.** The GIF decoder takes about 24.5 KB in one block, which
  leaves only a few KB of heap for everything else. The web interface's status block shows
  `heap` and `max_block`; if uploads or settings saves misbehave while a GIF is on screen,
  look there first. Not measured on the device.
- **Weather screen GIF is not clipped to its 80x80 box.** A larger GIF (say 240x240) draws over
  the rest of the screen. The web interface says "80x80"; the firmware does not enforce it.
- **Album pictures:** baseline JPEGs only (progressive ones are skipped with a message), EXIF
  rotation is ignored, a picture over about 1,900 pixels on a side is refused, GIFs are not
  scaled (a canvas wider than 480 pixels is refused, one larger than the screen is cropped),
  sub-folders are not read, and a GIF is cut off at the end of its slot rather than at the end
  of a loop. `/image` also holds the stock firmware's own pictures (for example `boot.jpg`):
  they show in the album and the web interface can delete them.
- **Stale weather looks like fresh weather.** If the weather cannot be refreshed, the last data
  stays on screen with no "last updated" hint; only a device that never got any data says so.
  Across midnight the forecast keeps its old days until the next successful fetch.
- **With the time zone on Auto, the clock shows UTC until the first weather answer** (it needs a
  city and a network): the offset comes from Open-Meteo, not from the NTP servers.
- **`status-esp.local` is a minimal mDNS responder.** It answers address questions only: the
  device does not appear in a network browser, two devices with the same name are not detected,
  and it does not run in rescue mode (use `192.168.4.1` there). A computer with no mDNS client
  (older Windows versions, some networks) cannot resolve `.local` names; the IP address always works.
- **The weather and city requests are plain HTTP** (the device has no TLS): the chosen city's
  coordinates travel in clear text on the local network and beyond.

