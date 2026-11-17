#!/usr/bin/env python3
"""A theme upload drops the rain frames before it measures the free space.

A full rain set fills LittleFS down to RAIN_FS_KEEP_FREE (48 KB) and a theme
upload keeps 16 KB, so without this a package over ~32 KB failed with "remove
a theme first" while ~750 KB was held by frames that can be fetched again
(review of PR #122). ThemeWeb.cpp does not build on the host, so this reads
the source: in receiveUpload()'s UPLOAD_FILE_START branch, rainRemoveAll()
is called under WITH_RAIN, after the upload is allowed and before
freeBytes() decides the budget.

    python3 tests/rain/check_theme_wiring.py
"""
import re
import sys

src = open("src/features/theme/ThemeWeb.cpp").read()
fail = []

if not re.search(r'#if WITH_RAIN\s*\n#include "RainClient.h"\s*\n#endif', src):
    fail.append('RainClient.h is not included under #if WITH_RAIN')

m = re.search(r"void receiveUpload\(\) \{(.*?)\n\}", src, re.S)
body = m.group(1) if m else ""
start = re.search(r"if \(up.status == UPLOAD_FILE_START\) \{(.*?)\} else if \(up.status == UPLOAD_FILE_WRITE\)", body, re.S)
branch = start.group(1) if start else ""
if not branch:
    fail.append("receiveUpload() has no UPLOAD_FILE_START branch")
else:
    call = re.search(r"#if WITH_RAIN\s*\n(?:\s*//[^\n]*\n)*\s*rainRemoveAll\(\);\s*\n#endif", branch)
    if not call:
        fail.append("rainRemoveAll() is not called under #if WITH_RAIN in UPLOAD_FILE_START")
    else:
        pos = call.start()
        for before in ("if (!uploadAllowed) return;", "installedCount() >= MaxThemes"):
            i = branch.find(before)
            if i < 0 or i > pos:
                fail.append(f"rainRemoveAll() comes before the check {before!r}")
        j = branch.find("freeBytes()")
        if j < 0 or j < pos:
            fail.append("rainRemoveAll() comes after freeBytes(): the budget misses the freed room")

for f in fail:
    print("FAIL theme wiring:", f)
if fail:
    sys.exit(1)
print("theme upload wiring: rain frames dropped before the free-space check")
