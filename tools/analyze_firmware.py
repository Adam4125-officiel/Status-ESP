"""Static analysis of an ESP8266/Arduino firmware (.bin): image headers,
segments, compressed (gzip) web pages and text strings.

Usage: python tools/analyze_firmware.py <firmware.bin> [output_dir]
Output: raw segments, decompressed gzip pages, strings.txt (offset: text).
Default output directory: <firmware without .bin>_analysis
"""
import os
import re
import struct
import sys
import zlib


def parse_image(d, off, out):
    magic, nseg, mode, sz, entry = struct.unpack_from("<BBBBI", d, off)
    print(f"[0x{off:06x}] magic=0x{magic:02x} segments={nseg} flash_mode={mode} "
          f"size/freq=0x{sz:02x} entry=0x{entry:08x}")
    p = off + 8
    for i in range(nseg):
        addr, ln = struct.unpack_from("<II", d, p)
        print(f"   seg{i}: load=0x{addr:08x} len=0x{ln:x} file_off=0x{p + 8:x}")
        with open(os.path.join(out, f"seg_{off:x}_{i}_{addr:08x}.bin"), "wb") as f:
            f.write(d[p + 8:p + 8 + ln])
        p += 8 + ln


def guess_ext(data):
    head = data[:2000].lower()
    if b"<html" in head or b"<!doctype" in head:
        return "html"
    if b"function" in head:
        return "js"
    return "bin"


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        d = f.read()
    out = sys.argv[2] if len(sys.argv) == 3 else os.path.splitext(sys.argv[1])[0] + "_analysis"
    os.makedirs(out, exist_ok=True)

    # Arduino ESP8266: eboot bootloader at 0x0, application at 0x1000
    for off in (0x0, 0x1000):
        if off < len(d) and d[off] == 0xE9:
            parse_image(d, off, out)

    for m in re.finditer(rb"\x1f\x8b\x08", d):
        o = m.start()
        try:
            data = zlib.decompressobj(16 + 15).decompress(d[o:])
        except zlib.error:
            continue
        if data:
            name = os.path.join(out, f"gz_{o:06x}.{guess_ext(data)}")
            with open(name, "wb") as f:
                f.write(data)
            print(f"gzip @0x{o:x} -> {len(data)} bytes: {os.path.basename(name)}")

    strs = [(m.start(), m.group().decode("latin1"))
            for m in re.finditer(rb"[\x20-\x7e\t\r\n]{5,}", d)]
    with open(os.path.join(out, "strings.txt"), "w", encoding="utf-8") as fh:
        for o, s in strs:
            fh.write(f"{o:06x}: {s!r}\n")
    print(f"{len(strs)} strings -> {out}")


if __name__ == "__main__":
    main()
