#!/usr/bin/env python3
"""CI gate: lib/ESP8266mDNS still matches the ESP8266 core it was copied from.

lib/ESP8266mDNS is a copy of the core's LEAmDNS library with one change (see
lib/ESP8266mDNS/README-SMALLTV.md). lib/ESP8266mDNS/UPSTREAM.json records the
platform pin, the framework package version and the SHA-256 of every upstream
file as the core ships it. This script fails when:

  1. platformio.ini pins a different espressif8266 platform than the record,
     or does not pin one at all (a platform bump is what changes the core);
  2. a file in the copy, other than the ones listed as patched, differs from
     the recorded hash, is missing, or is not listed at all;
  3. the installed framework-arduinoespressif8266 package has another version,
     or its libraries/ESP8266mDNS files differ from the recorded hashes
     (the patched file included: the record holds the ORIGINAL hash).

Check 3 needs the package; the firmware build downloads it. Without
--require-package a missing package is only a note. CI passes the flag.
Line endings are ignored (CRLF counts as LF). Python standard library only.

  python3 tools/check_mdns_copy.py --require-package
"""
import argparse
import hashlib
import json
import os
import re
import sys

LIB = os.path.join("lib", "ESP8266mDNS")
MANIFEST = "UPSTREAM.json"
PLATFORM_RE = re.compile(r"^\s*platform\s*=\s*(espressif8266\b.*?)\s*$")


def sha256(path):
    with open(path, "rb") as f:
        data = f.read().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def norm_platform(s):
    return re.sub(r"\s+", "", s)


def default_package():
    home = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.join(
        os.path.expanduser("~"), ".platformio")
    return os.path.join(home, "packages", "framework-arduinoespressif8266")


def walk(base):
    out = []
    for d, _, names in os.walk(base):
        for n in names:
            out.append(os.path.relpath(os.path.join(d, n), base).replace(os.sep, "/"))
    return out


def parse_ini(text):
    """{section: {key: value}}; a value keeps its continuation lines (joined
    by newlines); comment lines are dropped. Enough for platformio.ini."""
    secs, sec, key = {}, None, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line[0] in ";#":
            continue
        if raw[0] not in " \t" and line[0] == "[" and line.endswith("]"):
            sec, key = secs.setdefault(line[1:-1], {}), None
        elif sec is None:
            continue
        elif raw[0] in " \t" and key is not None:
            sec[key] += "\n" + line
        elif "=" in line:
            key, _, val = line.partition("=")
            key = key.strip()
            sec[key] = val.strip()
    return secs


REF_RE = re.compile(r"\$\{([^.}]+)\.([^}]+)\}")


def ini_value(secs, name, key, depth=0):
    """A key's value in a section or in what it extends, with ${sec.key}
    references filled in; None when it is not set."""
    if depth > 8 or name not in secs:
        return None
    sec = secs[name]
    if key in sec:
        return REF_RE.sub(
            lambda m: ini_value(secs, m.group(1), m.group(2), depth + 1) or "",
            sec[key])
    for parent in re.split(r"[,\s]+", sec.get("extends", "")):
        if parent:
            val = ini_value(secs, parent, key, depth + 1)
            if val is not None:
                return val
    return None


def ini_problems(text):
    """PlatformIO's default lib_compat_mode = soft checks only the framework,
    not the platform, so library.json's "platforms" keeps nothing out. Every
    env of another platform (the ESP32 ones) must name the copy in lib_ignore,
    or the ESP8266 sources are compiled there and fail."""
    secs = parse_ini(text)
    probs = []
    for name in sorted(secs):
        if not name.startswith("env:"):
            continue
        platform = ini_value(secs, name, "platform")
        if not platform or platform.startswith("espressif8266"):
            continue
        ignored = re.split(r"[,\s]+", ini_value(secs, name, "lib_ignore") or "")
        if "ESP8266mDNS" not in ignored:
            probs.append("[%s] does not run on the ESP8266 platform but has no "
                         "'ESP8266mDNS' in lib_ignore; the copy in lib/ would "
                         "be compiled for it" % name)
    return probs


def check(root, package, require_package):
    """Return a list of problems; empty means the copy still matches."""
    probs = []
    lib = os.path.join(root, LIB)
    try:
        with open(os.path.join(lib, MANIFEST)) as f:
            m = json.load(f)
        files = m["files"]
        patched = set(m["patched"])
        added = set(m["added"])
        want_platform = m["platform"]
        want_fw = m["framework_version"]
    except (OSError, ValueError, KeyError) as e:
        return ["cannot read %s: %s" % (os.path.join(LIB, MANIFEST), e)]

    # 1. the platform pin in platformio.ini
    pins = []
    try:
        with open(os.path.join(root, "platformio.ini")) as f:
            for line in f:
                mt = PLATFORM_RE.match(line)
                if mt:
                    pins.append(mt.group(1))
    except OSError as e:
        probs.append("cannot read platformio.ini: %s" % e)
    if not pins:
        probs.append("platformio.ini has no espressif8266 platform line")
    for p in pins:
        if "@" not in p:
            probs.append("platform '%s' is not pinned with @version" % p)
        elif norm_platform(p) != norm_platform(want_platform):
            probs.append("platformio.ini pins '%s' but the copy was made for "
                         "'%s'" % (p, want_platform))

    # 1b. no env of another platform compiles the copy
    try:
        with open(os.path.join(root, "platformio.ini")) as f:
            probs.extend(ini_problems(f.read()))
    except OSError:
        pass  # already reported above

    # 2. the copy in lib/
    for rel, h in sorted(files.items()):
        path = os.path.join(lib, rel)
        if not os.path.isfile(path):
            probs.append("copy is missing %s" % rel)
        elif rel not in patched and sha256(path) != h:
            probs.append("copy differs from upstream in %s (only %s may "
                         "differ)" % (rel, ", ".join(sorted(patched))))
    for rel in sorted(added):
        if not os.path.isfile(os.path.join(lib, rel)):
            probs.append("copy is missing our file %s" % rel)
    known = set(files) | added | {MANIFEST}
    for rel in sorted(walk(lib)):
        if rel not in known:
            probs.append("copy has %s, which UPSTREAM.json does not list" % rel)

    # 3. the installed package
    if package is None or not os.path.isdir(package):
        if require_package:
            probs.append("framework package not installed at %s" % package)
        else:
            print("note: framework package not installed, skipped installed-file check")
        return probs
    try:
        with open(os.path.join(package, "package.json")) as f:
            got_fw = json.load(f)["version"]
    except (OSError, ValueError, KeyError) as e:
        probs.append("cannot read the package version: %s" % e)
        got_fw = None
    if got_fw is not None and got_fw != want_fw:
        probs.append("installed framework is %s but the copy was made from %s"
                     % (got_fw, want_fw))
    pkg_lib = os.path.join(package, "libraries", "ESP8266mDNS")
    for rel, h in sorted(files.items()):
        path = os.path.join(pkg_lib, rel)
        if not os.path.isfile(path):
            probs.append("installed core has no %s" % rel)
        elif sha256(path) != h:
            probs.append("installed core's %s differs from the recorded "
                         "upstream hash" % rel)
    return probs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), ".."))
    ap.add_argument("--package", default=None,
                    help="framework-arduinoespressif8266 directory")
    ap.add_argument("--require-package", action="store_true")
    a = ap.parse_args(argv)
    package = a.package or default_package()
    probs = check(a.root, package, a.require_package)
    for p in probs:
        print("FAIL: " + p)
    if probs:
        print("lib/ESP8266mDNS no longer matches its pinned core. Compare the "
              "copy with the new core's libraries/ESP8266mDNS, re-apply or "
              "drop the fix, then update lib/ESP8266mDNS/UPSTREAM.json "
              "(see README-SMALLTV.md).")
        return 1
    print("ok: lib/ESP8266mDNS matches the pinned core")
    return 0


if __name__ == "__main__":
    sys.exit(main())
