"""Check an ESP8266/Arduino firmware (.bin) with the Arduino CRC (the one the eboot
bootloader verifies) before sending it to /update.

Usage: python tools/check_firmware.py <firmware.bin>
       python tools/check_firmware.py <full_flash_image.bin> <output.bin>
           -> extracts the firmware from a full flash image (4 MB)
"""
import hashlib
import struct
import sys

# elf2bin.py (Arduino) writes the image size and CRC at these offsets, i.e. in the
# first 8 bytes of the application's irom segment.
# (The classic ESP8266 image XOR checksum therefore does not match.)
CRC_SIZE_OFF, CRC_VAL_OFF = 0x1010, 0x1014

# CRC32, MSB first, polynomial 0x04C11DB7, init 0xFFFFFFFF, no final xor
TABLE = []
for i in range(256):
    c = i << 24
    for _ in range(8):
        c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
    TABLE.append(c & 0xFFFFFFFF)


def crc8266(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ TABLE[((crc >> 24) ^ b) & 0xFF]
    return crc


def extract(raw):
    """Return the firmware image (eboot + application) at the start of raw."""
    if len(raw) <= 0x1018 or raw[0] != 0xE9 or raw[0x1000] != 0xE9:
        raise ValueError("not an Arduino ESP8266 image (0xE9 expected at 0x0 and 0x1000)")
    size, val = struct.unpack_from("<II", raw, CRC_SIZE_OFF)
    if not 0x1000 < size <= len(raw):
        raise ValueError(f"invalid stored size: {size}")
    img = bytearray(raw[:size])
    img[CRC_SIZE_OFF:CRC_SIZE_OFF + 8] = b"\0" * 8
    if crc8266(img) != val:
        raise ValueError("invalid CRC: image corrupted or incomplete")
    return raw[:size]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    with open(sys.argv[1], "rb") as f:
        raw = f.read()
    try:
        app = extract(raw)
    except ValueError as e:
        sys.exit(f"INVALID: {e}")
    print(f"valid image: {len(app)} bytes, CRC OK, MD5 {hashlib.md5(app).hexdigest().upper()}")
    if len(sys.argv) == 3:
        with open(sys.argv[2], "wb") as f:
            f.write(app)
        print("written:", sys.argv[2])


if __name__ == "__main__":
    main()
