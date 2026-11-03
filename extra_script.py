Import("env")
import os
import subprocess
from pathlib import Path

# Windows: SCons's default cmd.exe-based SPAWN can mis-tokenize quoted/long
# build-tool command lines, dropping args. This re-tokenizes and dispatches directly.
PROJECT_DIR = env.subst("$PROJECT_DIR")

# The custom ESP8266 build keeps its recovery .stheme in program flash, outside
# LittleFS and the user theme catalog. Embed the package during this build so a
# bad filesystem or user theme cannot remove the recovery screen.
if env.subst("$PIOENV") in ("smalltv_custom", "smalltv_clean"):
    recovery = Path(PROJECT_DIR) / "examples" / "recovery-theme" / "recovery.stheme"
    build_dir = Path(env.subst("$BUILD_DIR"))
    if not build_dir.is_absolute():
        build_dir = Path(PROJECT_DIR) / build_dir
    build_dir = build_dir.resolve()
    build_dir.mkdir(parents=True, exist_ok=True)
    data = recovery.read_bytes()
    if len(data) < 8 or data[:4] != b"STH1":
        raise RuntimeError("Recovery theme must be a valid .stheme package")
    rows = [", ".join("0x%02x" % byte for byte in data[i:i + 16]) for i in range(0, len(data), 16)]
    header = "#pragma once\n#include <Arduino.h>\n"
    header += "static const uint8_t kRecoveryThemeData[] PROGMEM = {\n"
    header += ",\n".join("  " + row for row in rows)
    header += "\n};\nstatic constexpr uint32_t kRecoveryThemeSize = sizeof(kRecoveryThemeData);\n"
    (build_dir / "recovery_theme_data.h").write_text(header, encoding="ascii")
    env.Append(CPPPATH=[str(build_dir)])


def _tokenize(cmdline):
    # \"-escaped quotes (e.g. ESP8266's ARDUINO_BOARD_ID=\"esp12e\") must become a
    # literal " in the token, not get dropped like a bare ".
    tokens = []
    current = []
    in_quotes = False
    i, n = 0, len(cmdline)
    while i < n:
        c = cmdline[i]
        if c == "\\" and i + 1 < n and cmdline[i + 1] == '"':
            current.append('"')
            i += 2
            continue
        if c == '"':
            in_quotes = not in_quotes
        elif c.isspace() and not in_quotes:
            if current:
                tokens.append("".join(current))
                current = []
        else:
            current.append(c)
        i += 1
    if current:
        tokens.append("".join(current))
    return tokens


def _spawn(sh, escape, cmd, args, spawnenv):
    full_env = dict(os.environ)
    full_env.update(spawnenv)
    cmdline = " ".join(str(a) for a in args)
    real_args = _tokenize(cmdline)
    try:
        return subprocess.run(real_args, env=full_env, shell=False, cwd=PROJECT_DIR).returncode
    except OSError:
        return subprocess.run(cmdline, env=full_env, shell=True, cwd=PROJECT_DIR).returncode

# Windows-only: POSIX SCons spawns via sh with single-quote escaping, which
# the double-quote-only tokenizer above would mis-parse.
if os.name == "nt":
    env["SPAWN"] = _spawn
