# Hardware: GeekMagic SmallTV-Ultra

Legend: ✅ verified on the device, ❓ unknown

## Components
| Item | Detail | Status |
|---|---|---|
| SoC | ESP8266EX, 26 MHz crystal, ESP-12F-type module | ✅ |
| Flash | 4 MB, DIO mode, 40 MHz | ✅ |
| Display | ST7789 240x240, SPI **mode 3**, 40 MHz, correct RGB colours with no inversion | ✅ |
| Backlight | GPIO5, **inverted** PWM (low duty = bright) | ✅ |
| Button / touch | **None** | ✅ |
| Power | USB-C (power only, no USB serial port) | ✅ |
| Sensors, sound | None known | ❓ |

The device can therefore only be controlled over the network.

## Pinout
| GPIO | Function | Note |
|---|---|---|
| 13 | SPI MOSI (display) | |
| 14 | SPI SCLK (display) | |
| 0 | Display DC | **Boot pin**: never drive it low at boot |
| 2 | Display RST | **Boot pin**: must be high at boot |
| 5 | Backlight (inverted PWM) | |
| - | Display CS | Not wired (tied to ground) |
| 4, 12, 15, 16 | Free? | ❓ not verified, do not assume anything |

## Flash layout (same as the stock firmware, "4M3M")
| Address | Size | Content |
|---|---|---|
| `0x000000` | 4 KB | Arduino bootloader `eboot` |
| `0x001000` | ~1 MB | Application (running firmware) plus room for the next update |
| `0x100000` | `0x2FA000` (3,121,152 bytes) | LittleFS: files (images, `.json` settings, the stock Wi-Fi credentials...) |
| `0x3FA000` | 24 KB | System area: EEPROM, RF calibration, the SDK's Wi-Fi configuration |

The stock firmware's `/space.json` returns `total: 3121152`, which confirms this layout.

## Updating over Wi-Fi: the size constraint
An Arduino over-the-air update writes the new image **into the free space behind the
current image**, before the LittleFS area, and the bootloader then copies it into place.

| Installed firmware | Size | Free space for the next update |
|---|---|---|
| GeekMagic 9.0.50 / 9.0.51 | 505,200 bytes | 540,672 bytes |
| Status-ESP 0.4.0-rc.2 | 506,384 bytes | ~540,000 bytes |

Hence the two rules of the project:
1. our firmware must stay **below ~520 KB** so it can be installed from the stock firmware;
2. it must leave **at least 505,200 bytes** free so you can go back to the stock firmware.

The enforced limit is `firmware.bin` < 520,000 bytes.

## Memory (RAM)
The ESP8266 has about 80 KB of RAM. The 0.3.0 build uses about 37.7 KB of it statically (the
`RAM:` line of `pio run`); the rest is the heap, shared by the network stack, the web server
and, while a GIF is on screen, the GIF decoder (about 24.5 KB in one contiguous block). That is
why the decoder is allocated only while a GIF is shown and never alongside a weather fetch (rule
10 in `CLAUDE.md`). The web interface's status block reports the free heap and the largest free
block of the running device.
