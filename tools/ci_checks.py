#!/usr/bin/env python3
"""CI gates for smalltv-mod. Python stdlib only.

  size    Compare each env's firmware.bin with its OTA app slot and fail when
          the free space drops below a threshold.
  assets  Check that every UPDATE_ASSET string in src/config.h is a file name
          that the workflow stages into dist/.

Run from the repo root:

  python3 tools/ci_checks.py size
  python3 tools/ci_checks.py assets

Where the numbers come from (nothing is hardcoded per env):
  - The env list and each env's partition table or linker script come from
    `pio project config --json-output`, so extends/interpolation is resolved
    by PlatformIO itself. Pass --envs to restrict the list.
  - ESP32 envs: the smallest app partition in board_build.partitions (CSV).
  - ESP8266 envs: the irom0_0_seg length in board_build.ldscript, read from
    the installed framework-arduinoespressif8266 package. That length is what
    the linker enforces, and with a 4 MB chip it is smaller than half of the
    free flash, so it is also the OTA limit.
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

DEFAULT_MIN_FREE = 8192


def parse_size(text):
    t = text.strip().lower()
    mult = 1
    if t.endswith("k"):
        mult, t = 1024, t[:-1]
    elif t.endswith("m"):
        mult, t = 1024 * 1024, t[:-1]
    return int(t, 0) * mult


def app_slot_from_csv(path):
    """Smallest app partition (ota_* or factory) in an ESP-IDF partition CSV."""
    sizes = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            cols = [c.strip() for c in line.split(",")]
            if len(cols) < 5 or cols[1] != "app":
                continue
            sizes.append(parse_size(cols[4]))
    if not sizes:
        raise ValueError("no app partition in %s" % path)
    return min(sizes)


def app_slot_from_ld(ld_name, pio_home):
    """Length of irom0_0_seg in an ESP8266 Arduino linker script."""
    pattern = os.path.join(pio_home, "packages", "framework-arduinoespressif8266",
                           "tools", "sdk", "ld", ld_name)
    hits = glob.glob(pattern)
    if not hits:
        raise ValueError("linker script %s not found under %s (is the ESP8266 "
                         "framework installed?)" % (ld_name, pattern))
    with open(hits[0], encoding="utf-8") as f:
        m = re.search(r"irom0_0_seg\s*:\s*org\s*=\s*\S+,\s*len\s*=\s*(\S+)", f.read())
    if not m:
        raise ValueError("no irom0_0_seg in %s" % hits[0])
    return int(m.group(1), 0)


def load_envs(pio):
    out = subprocess.run([pio, "project", "config", "--json-output"],
                         check=True, capture_output=True, text=True).stdout
    envs = {}
    for section, items in json.loads(out):
        if section.startswith("env:"):
            envs[section[4:]] = {k: v for k, v in items}
    return envs


def env_slot(opts, pio_home):
    part = opts.get("board_build.partitions")
    if part:
        return app_slot_from_csv(part), part
    ld = opts.get("board_build.ldscript")
    if ld:
        return app_slot_from_ld(ld, pio_home), ld
    raise ValueError("env sets neither board_build.partitions nor "
                     "board_build.ldscript, no slot size to read")


def cmd_size(args):
    pio_home = args.pio_home or os.environ.get("PLATFORMIO_CORE_DIR") \
        or os.path.join(os.path.expanduser("~"), ".platformio")
    envs = load_envs(args.pio)
    names = args.envs.split(",") if args.envs else list(envs)
    rows, bad = [], []
    for name in names:
        if name not in envs:
            bad.append("%s: not an env in platformio.ini" % name)
            continue
        try:
            slot, source = env_slot(envs[name], pio_home)
        except (ValueError, OSError) as e:
            bad.append("%s: %s" % (name, e))
            continue
        binp = os.path.join(args.build_dir, name, "firmware.bin")
        if not os.path.isfile(binp):
            if args.allow_missing:
                rows.append((name, None, slot, None, source, "skipped (no firmware.bin)"))
            else:
                bad.append("%s: %s is missing" % (name, binp))
            continue
        size = os.path.getsize(binp)
        free = slot - size
        status = "ok"
        if free < args.min_free:
            status = "FAIL"
            bad.append("%s: %d B free in a %d B slot (%s), need at least %d B"
                       % (name, free, slot, source, args.min_free))
        rows.append((name, size, slot, free, source, status))

    hdr = ("image", "size", "slot", "free", "free %", "slot source", "status")
    table = [hdr]
    for name, size, slot, free, source, status in rows:
        if size is None:
            table.append((name, "-", str(slot), "-", "-", source, status))
        else:
            table.append((name, str(size), str(slot), str(free),
                          "%.1f" % (100.0 * free / slot), source, status))
    widths = [max(len(r[i]) for r in table) for i in range(len(hdr))]
    for i, r in enumerate(table):
        print("  ".join(c.ljust(widths[j]) if j in (0, 5, 6) else c.rjust(widths[j])
                        for j, c in enumerate(r)))
        if i == 0:
            print("  ".join("-" * w for w in widths))
    print("minimum free: %d B" % args.min_free)
    for b in bad:
        print("::error::size gate: " + b)
    return 1 if bad else 0


def cmd_assets(args):
    with open(args.config_h, encoding="utf-8") as f:
        wanted = re.findall(r'#\s*define\s+UPDATE_ASSET\s+"([^"]+)"', f.read())
    with open(args.workflow, encoding="utf-8") as f:
        staged = set(re.findall(r"^\s*copy\s+\S+\s+dist/(\S+)\s*$", f.read(), re.M))
    if not wanted:
        print("::error::asset check: no UPDATE_ASSET found in %s" % args.config_h)
        return 1
    if not staged:
        print("::error::asset check: no `copy <src> dist/<name>` lines found in %s"
              % args.workflow)
        return 1
    bad = False
    for a in wanted:
        ok = a in staged
        print("%-40s %s" % (a, "staged" if ok else "NOT STAGED"))
        if not ok:
            bad = True
            print("::error::asset check: UPDATE_ASSET \"%s\" in %s is not staged "
                  "into dist/ by %s (staged: %s). A device on that variant would "
                  "find no matching release asset and could never self-update."
                  % (a, args.config_h, args.workflow, ", ".join(sorted(staged))))
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("size", help="firmware.bin vs app slot")
    s.add_argument("--build-dir", default=".pio/build")
    s.add_argument("--min-free", type=int, default=DEFAULT_MIN_FREE)
    s.add_argument("--envs", help="comma separated; default: every env in platformio.ini")
    s.add_argument("--pio", default="pio")
    s.add_argument("--pio-home", help="PlatformIO core dir (default ~/.platformio)")
    s.add_argument("--allow-missing", action="store_true",
                   help="skip envs without a firmware.bin (local use)")
    s.set_defaults(fn=cmd_size)

    a = sub.add_parser("assets", help="UPDATE_ASSET names vs dist/ copy lines")
    a.add_argument("--config-h", default="src/config.h")
    a.add_argument("--workflow", default=".github/workflows/build.yml")
    a.set_defaults(fn=cmd_assets)

    args = p.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
