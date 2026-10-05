"""Prepare le paquet de release a joindre sur GitHub, au meme format que GeekMagic :
un zip contenant le .bin a envoyer sur /update, son MD5 et une notice.

Usage (apres compilation) : python tools/make_release.py [--tag vX.Y.Z]
  --tag : verifie que le tag Git correspond a FW_VERSION (utilise par la CI)
Resultat : dist/SmallTV-Ultra-Custom-<version>.zip
"""
import hashlib
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
from check_firmware import extract  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_BIN = os.path.join(ROOT, ".pio", "build", "smalltv-ultra", "firmware.bin")
MAX_SIZE = 520000  # installable depuis le firmware d'origine (540 672 o libres)

NOTICE = """SmallTV-Ultra Custom {version}

/!\\ UNIQUEMENT pour le GeekMagic SmallTV-Ultra (ESP8266). Pas pour SmallTV, Pro, HelloCubic...

Installation :
 1. Ouvrir http://<ip-de-l-ecran>/update dans un navigateur
    (l'IP est dans les reglages du firmware d'origine, ou affichee sur l'ecran).
 2. Choisir {bin} puis valider. Ne pas couper le courant.
 3. L'ecran redemarre et affiche l'adresse de sa nouvelle interface.

Verifier le MD5 avant l'envoi (Windows : Get-FileHash {bin} -Algorithm MD5).

Retour au firmware GeekMagic : envoyer leur .bin officiel sur la meme page /update.
Les images et reglages d'origine sont conserves.
"""


def read_version():
    ini = open(os.path.join(ROOT, "platformio.ini"), encoding="utf-8").read()
    m = re.search(r'FW_VERSION=\\"Custom-([0-9][^\\"]*)\\"', ini)
    if not m:
        sys.exit("FW_VERSION introuvable dans platformio.ini")
    return m.group(1)


def main():
    version = read_version()
    if len(sys.argv) == 3 and sys.argv[1] == "--tag":
        if sys.argv[2] != f"v{version}":
            sys.exit(f"le tag {sys.argv[2]} ne correspond pas a FW_VERSION (v{version})")
    elif len(sys.argv) != 1:
        sys.exit(__doc__)
    if not os.path.exists(BUILD_BIN):
        sys.exit("firmware.bin introuvable : compiler d'abord (tools/build.ps1 ou tools/build.sh)")
    raw = open(BUILD_BIN, "rb").read()
    app = extract(raw)  # leve une erreur si le CRC est invalide
    if len(app) != len(raw):
        sys.exit("firmware.bin contient des donnees en trop apres l'image")
    if len(raw) > MAX_SIZE:
        sys.exit(f"firmware trop gros : {len(raw)} > {MAX_SIZE} octets")
    if f"Custom-{version}".encode() not in raw:
        sys.exit("la version du binaire ne correspond pas a platformio.ini : recompiler")

    name = f"SmallTV-Ultra-Custom-{version}"
    bin_name = f"FW-{name}.bin"
    md5 = hashlib.md5(raw).hexdigest()
    os.makedirs(os.path.join(ROOT, "dist"), exist_ok=True)
    zip_path = os.path.join(ROOT, "dist", f"{name}.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{name}/{bin_name}", raw)
        z.writestr(f"{name}/md5sum.txt", f"{md5}  {bin_name}\n")
        z.writestr(f"{name}/LISEZMOI.txt", NOTICE.format(version=version, bin=bin_name))
    print(f"{zip_path}\n  {bin_name} : {len(raw)} octets, MD5 {md5}")


if __name__ == "__main__":
    main()
