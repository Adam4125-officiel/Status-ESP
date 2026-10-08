# Roadmap

Features planned for later versions. Nothing here exists yet; each item is an idea to design with the
project owner before it is built. When an item ships, remove it from this file (the CHANGELOG is the
record of what exists).

Version numbers follow [docs/releasing.md](docs/releasing.md): new features bump the minor number.

## Display
- **Smooth (anti-aliased) fonts** loaded from LittleFS, as more clock fonts. The built-in
  TFT_eSPI fonts are all the firmware has today (the "Large" font is Font 8).
- **Older footers behind the plastic.** The Status-Portal and Resources screens draw their last line (the network
  rates, "Page 1/2") at y 218..234, which the plastic over a SmallTV-Ultra's glass may partly hide, as it did the
  hourly forecast's hour labels (fixed in 1.1.0-rc.2, with the VM footer). Move them up to y 206 and hold them to
  the tests' safe area when the owner reports it.
- **Richer weather**: more forecast days (the hourly forecast shipped in 1.1.0), wind and a "feels like" row in the hourly strip.


## Memory
- **GIFs and the heap.** A GIF now needs 26 KB free to open (it was 31 KB, with 0.5 KB of slack
  at idle), and yields to web requests (see CLAUDE.md, rule 10). Still open: the decoder is one
  24.3 KB block, so a GIF plays with about 6 KB of heap left, and a fetch (weather, portal)
  closes it and restarts it. Lowering static RAM (42.6 KB, of which the Status-Portal cache is
  2.4 KB and about 10 KB is string literals the toolchain keeps in RAM, 3.5 KB of them in
  `web.cpp`) would give every GIF more room. Not seen on the device with the album's own pictures:
  progressive JPEGs are skipped with a message, and the baseline ones all decode with the
  decoder's 3.5 KB workspace (checked on a PC against every picture on the device).

## Updates
- **On-device auto-updater**: the device reads
  `https://github.com/Adam4125-officiel/Status-ESP/releases/latest/download/version.json` (stable releases
  only; schema in [docs/releasing.md](docs/releasing.md)), and offers or installs the new `.bin`.
  **Blocked by size today**: GitHub only serves HTTPS, which needs BearSSL, and BearSSL does not fit
  in what is left. 0.3.0-rc.3 is 489,488 bytes against the 520,000-byte limit (about 30 KB of
  flash left, and the next feature, the Status-Portal client, also has to fit), and the TLS code plus its buffers would cost more than that in flash and a large
  contiguous block of the little heap that is left (see the GIF memory note below). Settle this
  first, then: following the GitHub redirect to the download host, MD5 check with the `Updater`
  class, never installing a pre-release automatically, and `/update` must stay reachable
  whatever the updater does.

## Status-Portal
- **A progress bar for Jellyfin's tasks.** The band says what Jellyfin is doing ("2 transcodes", the name
  of a trickplay or scan task) but not how far along it is: Status-Portal's Jellyfin cache keeps names only.
  It would need the portal to keep `CurrentProgressPercentage` and send it, then a percentage in the band.

## Settings
- Import of the stock firmware's settings (city, units, time format...) on first boot, once their file
  formats are known.

## Known limitations
These are not planned features but things that are missing or rough today. Fix them when they
get in the way; remove a line when it is no longer true.

- **Only part of it has been tested on a device.** Checked on the real device: the weather fetch, the
  backup download and restore, saving and rotating themes, the Status-Portal link, `/update` with a GIF
  playing, a GIF opening and staying open. Still unchecked on it, because nobody can see the screen from
  the development machine: how any theme looks on the real display, the paged Resources screen with GPUs,
  the latency beside the status and Jellyfin's band with real data (the owner's portal was not yet on
  1.11.0), GIF decoding speed, `status-esp.local` on a real network, and the password prompt in a real
  browser. The drawing, the parsing and the album logic were run on a PC against a stand-in (`tests/host/`).
- **Memory while a GIF plays is tight.** The GIF decoder takes about 24.5 KB in one block, which
  leaves only about 6 KB of heap for everything else (a web request that would be short closes the GIF first). The web interface's status block shows
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
- **The backup is one long blocking transfer.** Downloading it freezes the screen and every other
  request for as long as it takes (15 s for 1.8 MB), and a restore does the same while it is
  uploaded. Empty folders are not saved, a file whose path is over 99 characters is left out,
  and a restore stops writing files at the first storage error but keeps what it already wrote.
- **The weather and city requests are plain HTTP** (the device has no TLS): the chosen city's
  coordinates travel in clear text on the local network and beyond.

