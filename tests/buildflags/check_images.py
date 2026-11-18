#!/usr/bin/env python3
"""Checks the built images against the GitHub update build option.

    python3 tests/buildflags/check_images.py [build dir]      (default .pio/build)

The standard image (smalltv) must hold the download code, and the image built
with -D FW_NO_GITHUB_UPDATE (smalltv_noupdate) must hold none of it. The
download code is recognised by two texts that only it has (see MARKERS). If
the standard image does not hold them, the check fails too: it can no longer
see the code it is looking for, so its "absent" would mean nothing.

Prints both sizes and the difference. Exit 0 when all is as expected.
"""
import os
import sys

STANDARD = "smalltv"
NO_UPDATE = "smalltv_noupdate"
# Texts of src/OtaUpdate.cpp that are only in the download code.
MARKERS = ("server reported no update", "not enough heap even at boot")


def load(build_dir, env):
    path = os.path.join(build_dir, env, "firmware.bin")
    if not os.path.isfile(path):
        raise SystemExit("FAIL: %s is missing (build the env first)" % path)
    with open(path, "rb") as f:
        return f.read()


def main(argv):
    build_dir = argv[1] if len(argv) > 1 else os.path.join(".pio", "build")
    standard = load(build_dir, STANDARD)
    noupdate = load(build_dir, NO_UPDATE)
    bad = []
    for marker in MARKERS:
        raw = marker.encode()
        if raw not in standard:
            bad.append("the %s image does not hold \"%s\": the check cannot see the download code" % (STANDARD, marker))
        if raw in noupdate:
            bad.append("the %s image holds \"%s\": download code is compiled in" % (NO_UPDATE, marker))
    print("%-18s %9d B" % (STANDARD, len(standard)))
    print("%-18s %9d B" % (NO_UPDATE, len(noupdate)))
    print("difference         %9d B smaller with the update off" % (len(standard) - len(noupdate)))
    for line in bad:
        print("::error::" + line)
    if not bad:
        print("download code: in %s, not in %s" % (STANDARD, NO_UPDATE))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
