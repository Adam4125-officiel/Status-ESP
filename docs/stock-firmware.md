# GeekMagic stock firmware (Ultra-V9.0.50 / 9.0.51): analysis notes

Static analysis done with `tools/analyze_firmware.py`. The source code is not public: only
the compiled binary is available. The firmware itself is **not redistributed here**
(proprietary); version 9.0.50 is available from the
[official repository](https://github.com/GeekMagicClock/smalltv-ultra).

## General
- Arduino ESP8266 (NONOS SDK built in 2019), LittleFS, 505,200-byte image.
- Identification: `GET /v.json` returns `{"m": "SmallTV-Ultra","v":"Ultra-V9.0.51"}`.
  Status-ESP answers with the same shape, for example
  `{"m":"SmallTV-Ultra","v":"Status-ESP-<version>"}`.
- Version 9.0.51 (the factory version) is not published anywhere. It really does differ
  from 9.0.50 (about 351,000 bytes differ) despite the identical size.
- Web interface: 4 HTML/JS pages stored gzip-compressed inside the firmware:
  `time.html`, `weather.html`, `settings.html`, `image.html`.

## HTTP API (port 80, no authentication)
| Route | Purpose |
|---|---|
| `GET /set?<param>=<value>` | Changes a setting (see the list below) |
| `GET /<name>.json` | Reads a setting |
| `POST /doUpload?dir=<folder>` | Uploads a file (image/GIF) into LittleFS |
| `GET /delete?file=<path>` | Deletes a file |
| `GET /filelist?dir=<folder>` | Lists a folder (HTML) |
| `GET /space.json` | `{"total":...,"free":...}` for LittleFS |
| `GET/POST /update` | Firmware update (`ESP8266HTTPUpdateServer`, `firmware` field); the "filesystem" form is hidden in an HTML comment |
| `/wifisave`, `/generate_204`, `/hotspot-detect.html`, `/fwlink` | Wi-Fi configuration captive portal |

`/set` parameters found: `brt`, `theme`, `theme_list`, `sw_en`, `theme_interval`,
`tz_auto`, `tz_offset`, `hour`, `font`, `colon`, `ntp`, `day`, `hc`/`mc`/`sc` (hour/minute/
second colours), `time_interval`, `yr`/`mth`/`day`, `key`, `fkey`,
`w_u`/`t_u`/`p_u` (units), `cd1`/`cd2`, `w_i`, `gif`, `img`, `i_i`, `autoplay`,
`clear=gif|image`, `t1`/`t2`/`b1`/`b2`/`en` (night mode), `reboot=1`, `reset=1`.

Settings JSON files (in LittleFS): `/config.json` (Wi-Fi: `{"a":"<ssid>","p":"<password>"}`,
password masked by the API), `/city.json`, `/key.json`, `/fkey.json`, `/unit.json`,
`/ntp.json`, `/tz.json`, `/dst.json`, `/brt.json`, `/timebrt.json`, `/delay.json`,
`/font.json`, `/gif.json`, `/img.json`, `/album.json`, `/app.json`, `/theme_list.json`,
`/hour12.json`, `/rotation.json`, `/colon.json`, `/day.json`, `/timecolor.json`,
`/lon.json`, `/w_i.json`, `/space.json`, `/v.json`, `/wifi.json`.

Other files in use: `/image/...` (photos), `/gif/...` (animations),
`/image/boot.jpg|gif`, `/Alibaba20.vlw` (smooth font).

Status-ESP keeps its own settings in `/custom.json`, a name that does not collide with
any of the stock files above.

## External services contacted
- Weather: `api.openweathermap.org` (user API key through `/set?key=`) and `api.open-meteo.com`.
- Time: NTP, `ntp.aliyun.com` by default.
- The binary also contains 9 hexadecimal strings of 32 characters, possibly default API keys
  or fingerprints. They are deliberately **not reproduced** here, and should not be added
  to this repository: anything that looks like an API key stays out.

## Notable points
- `/update` has no password: any machine on the local network can reflash the device.
- Status-ESP re-reads `/config.json` to reuse the stock Wi-Fi credentials.
