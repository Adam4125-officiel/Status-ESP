# PlatformIO pre-build script (platformio.ini: extra_scripts = pre:tools/version.py).
#
# This is the ONLY place where the firmware version enters the build: it reads the
# VERSION file at the repository root, validates it, and defines FW_VERSION as a C
# string literal (e.g. "0.3.0-rc.1"). src/main.cpp builds the full name from it.
# An invalid VERSION aborts the build instead of producing a mislabelled image.

import os
import re
import sys

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$")

version_file = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")  # noqa: F821

try:
    with open(version_file, "r", encoding="ascii") as f:
        version = f.read().strip()
except (OSError, UnicodeDecodeError) as exc:
    sys.stderr.write("tools/version.py: cannot read %s: %s\n" % (version_file, exc))
    env.Exit(1)  # noqa: F821

if not VERSION_RE.match(version):
    sys.stderr.write(
        "tools/version.py: invalid VERSION %r in %s\n"
        "  expected MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-rc.N, "
        "with no leading 'v' (e.g. 0.3.0 or 0.3.0-rc.1)\n" % (version, version_file)
    )
    env.Exit(1)  # noqa: F821

env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
print("Status-ESP version: %s" % version)
