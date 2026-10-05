"""Verifie un firmware ESP8266/Arduino (.bin) avec le CRC Arduino (celui que le
bootloader eboot controle) avant de l'envoyer sur /update.

Usage : python tools/check_firmware.py <firmware.bin>
        python tools/check_firmware.py <image_flash_complete.bin> <sortie.bin>
            -> extrait le firmware d'une image flash complete (4 Mo)
"""
import hashlib
import struct
import sys

# elf2bin.py (Arduino) ecrit la taille et le CRC de l'image a ces adresses,
# c'est-a-dire dans les 8 premiers octets du segment irom de l'application.
# (Le checksum XOR classique des images ESP8266 ne correspond donc pas.)
CRC_SIZE_OFF, CRC_VAL_OFF = 0x1010, 0x1014

# CRC32 MSB-first, polynome 0x04C11DB7, init 0xFFFFFFFF, sans xor final
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
    """Renvoie l'image firmware (eboot + application) contenue au debut de raw."""
    if len(raw) <= 0x1018 or raw[0] != 0xE9 or raw[0x1000] != 0xE9:
        raise ValueError("pas d'image Arduino ESP8266 (0xE9 attendu a 0x0 et 0x1000)")
    size, val = struct.unpack_from("<II", raw, CRC_SIZE_OFF)
    if not 0x1000 < size <= len(raw):
        raise ValueError(f"taille stockee invalide : {size}")
    img = bytearray(raw[:size])
    img[CRC_SIZE_OFF:CRC_SIZE_OFF + 8] = b"\0" * 8
    if crc8266(img) != val:
        raise ValueError("CRC invalide : image corrompue ou incomplete")
    return raw[:size]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    raw = open(sys.argv[1], "rb").read()
    try:
        app = extract(raw)
    except ValueError as e:
        sys.exit(f"INVALIDE : {e}")
    print(f"image valide : {len(app)} octets, CRC OK, MD5 {hashlib.md5(app).hexdigest().upper()}")
    if len(sys.argv) == 3:
        open(sys.argv[2], "wb").write(app)
        print("ecrit :", sys.argv[2])


if __name__ == "__main__":
    main()
