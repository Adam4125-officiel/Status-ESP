# PlatformIO post-build script (platformio.ini: extra_scripts = post:tools/linkflags.py).
#
# The Arduino ESP8266 core links `-u _printf_float -u _scanf_float`, which force newlib's
# floating-point printf and scanf (and, through scanf, strtod) into every firmware: about
# 6 KB that this project does not need. Nothing here calls printf("%f") (units.h's
# formatFixed() does the few decimals by hand) or scanf. Remove both so the size limit
# (see CLAUDE.md, rule 4) has room for features.
#
# If a "%f" ever gets added again it prints NOTHING (an empty string) instead of failing
# loudly: use units::formatFixed() instead. tests are the host builds in CLAUDE.md section 6.

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

DROP = ("_printf_float", "_scanf_float")

flags = list(env["LINKFLAGS"])  # noqa: F821
kept = []
i = 0
while i < len(flags):
    if flags[i] == "-u" and i + 1 < len(flags) and flags[i + 1] in DROP:
        i += 2
        continue
    kept.append(flags[i])
    i += 1
env.Replace(LINKFLAGS=kept)  # noqa: F821
print("Status-ESP: removed -u _printf_float / -u _scanf_float (%d link flags dropped)" % (len(flags) - len(kept)))
