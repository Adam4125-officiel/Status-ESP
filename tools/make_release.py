"""Validate a built firmware and prepare the release assets.

Usage (after a build): python tools/make_release.py [--tag vX.Y.Z] [--bin PATH]

  --tag  also check that the given Git tag equals "v" + the content of VERSION
         (release.sh and the CI use this guard)
  --bin  use another firmware file instead of .pio/build/smalltv-ultra/firmware.bin
         (handy for tests)

The version comes from the VERSION file at the repository root (the single source
of truth, also read by tools/version.py at build time).

The image is refused unless it is a valid Arduino ESP8266 image (CRC), has no
trailing data, is smaller than 520 000 bytes, and contains the string
"Status-ESP-<version>" (otherwise it was built from another VERSION: rebuild).

Output, in dist/v<version>/ (recreated from scratch on every run):
  Status-ESP-<version>.bin   the image to send to the device's /update page
  version.json               machine-readable description of the release
  checksums.txt              MD5 and SHA256 in BSD-tag format; verify with
                             "cksum -c checksums.txt" (GNU coreutils 9+) or by
                             hand with Get-FileHash on Windows
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_firmware import extract  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_BIN = os.path.join(ROOT, ".pio", "build", "smalltv-ultra", "firmware.bin")
MAX_SIZE = 520000  # exclusive: installable from the stock firmware (540 672 bytes free)

NAME = "Status-ESP"
TARGET = "smalltv-ultra"
REPO = "Adam4125-officiel/Status-ESP"
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$")


def read_version():
    path = os.path.join(ROOT, "VERSION")
    try:
        with open(path, encoding="ascii") as f:
            version = f.read().strip()
    except (OSError, UnicodeDecodeError) as e:
        sys.exit(f"cannot read VERSION ({path}): {e}")
    if not VERSION_RE.match(version):
        sys.exit(f"invalid VERSION {version!r}: expected MAJOR.MINOR.PATCH or "
                 "MAJOR.MINOR.PATCH-rc.N, with no leading 'v' (e.g. 0.3.0 or 0.3.0-rc.1)")
    return version


def contains_version_string(raw, version):
    """True if the binary holds "Status-ESP-<version>" as a whole version.

    A plain substring test is not enough: "Status-ESP-0.3.0" is also a prefix of
    "Status-ESP-0.3.0-rc.1", and "...-rc.1" a prefix of "...-rc.10". The string must
    therefore not be followed by another character that could continue a version.
    """
    pattern = re.escape(f"{NAME}-{version}".encode()) + rb"(?![0-9A-Za-z.+\-])"
    return re.search(pattern, raw) is not None


def main():
    ap = argparse.ArgumentParser(
        description="Validate a built firmware and prepare the release assets "
                    "in dist/v<version>/.")
    ap.add_argument("--tag", metavar="vX.Y.Z",
                    help='check that this tag equals "v" + VERSION')
    ap.add_argument("--bin", metavar="PATH", default=BUILD_BIN,
                    help="firmware file to package (default: the PlatformIO build output)")
    args = ap.parse_args()

    version = read_version()
    tag = f"v{version}"
    if args.tag is not None and args.tag != tag:
        sys.exit(f"tag {args.tag} does not match VERSION: expected {tag}")

    if not os.path.isfile(args.bin):
        sys.exit(f"firmware not found: {args.bin}\n"
                 "build first (tools/build.sh or tools/build.ps1)")
    with open(args.bin, "rb") as f:
        raw = f.read()

    try:
        app = extract(raw)  # raises ValueError if the image or its CRC is invalid
    except ValueError as e:
        sys.exit(f"{args.bin}: {e}")
    if len(app) != len(raw):
        sys.exit(f"{args.bin} has {len(raw) - len(app)} bytes of trailing data "
                 "after the image: refusing to release it")
    if len(raw) >= MAX_SIZE:
        sys.exit(f"firmware too big: {len(raw)} bytes, must be below {MAX_SIZE}")
    if not contains_version_string(raw, version):
        sys.exit(f"version string not found: the binary does not contain "
                 f"\"{NAME}-{version}\", so it was built from a different VERSION. "
                 "Rebuild (tools/build.sh or tools/build.ps1) and try again.")

    bin_name = f"{NAME}-{version}.bin"
    md5 = hashlib.md5(raw).hexdigest()
    sha256 = hashlib.sha256(raw).hexdigest()
    info = {
        "name": NAME,
        "version": version,
        "tag": tag,
        "prerelease": "-rc." in version,
        "target": TARGET,
        "file": bin_name,
        "size": len(raw),
        "md5": md5,
        "sha256": sha256,
        "url": f"https://github.com/{REPO}/releases/download/{tag}/{bin_name}",
    }

    out_dir = os.path.join(ROOT, "dist", tag)
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir)
    paths = {
        "bin": os.path.join(out_dir, bin_name),
        "json": os.path.join(out_dir, "version.json"),
        "sums": os.path.join(out_dir, "checksums.txt"),
    }
    with open(paths["bin"], "wb") as f:
        f.write(raw)
    # Compact JSON on purpose: the future on-device updater may parse it with a
    # very small parser (the md5 is what the ESP8266 Updater's setMD5() takes).
    with open(paths["json"], "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(info, separators=(",", ":")) + "\n")
    with open(paths["sums"], "w", encoding="utf-8", newline="\n") as f:
        f.write(f"MD5 ({bin_name}) = {md5}\n")
        f.write(f"SHA256 ({bin_name}) = {sha256}\n")

    print(f"{NAME} {tag}" + ("  (pre-release)" if info["prerelease"] else ""))
    for p in paths.values():
        print("  " + os.path.relpath(p, ROOT))
    print(f"  size   {len(raw)} bytes (limit {MAX_SIZE})")
    print(f"  md5    {md5}")
    print(f"  sha256 {sha256}")


if __name__ == "__main__":
    main()
