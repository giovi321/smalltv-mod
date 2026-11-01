#!/usr/bin/env python3
"""Regenerate src/webui.h from src/webui.html.

The web UI ships as a gzip-compressed byte array in PROGMEM and is served with
`Content-Encoding: gzip` (see handleRoot in src/WebPortal.cpp). src/webui.html
is the source of truth — edit it, then rerun:

    python tools/gzip_webui.py

Python 3 stdlib only. Output is deterministic (gzip header mtime=0), so
re-running without changes leaves webui.h untouched on the same machine.
Another zlib build (zlib-ng, say) can compress to other bytes, so CI runs
`python tools/gzip_webui.py --check`, which compares what webui.h
decompresses to with webui.html instead.
"""
import gzip
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src" / "webui.html"
OUT = ROOT / "src" / "webui.h"

HEADER = """\
// GENERATED FILE -- do not edit by hand.
// Produced by tools/gzip_webui.py from src/webui.html (the source of truth).
// Regenerate after editing the HTML:  python tools/gzip_webui.py
//
// Gzip-compressed single-page config UI, served from PROGMEM with
// `Content-Encoding: gzip` by handleRoot in WebPortal.cpp.
#pragma once
#include <Arduino.h>

static const uint8_t WEBUI_HTML_GZ[] PROGMEM = {
"""


def html() -> bytes:
    # The repo stores LF; a Windows checkout with core.autocrlf has CRLF.
    return SRC.read_bytes().replace(b"\r\n", b"\n")


def check() -> int:
    body = OUT.read_text(encoding="utf-8").split("PROGMEM = {", 1)[1]
    blob = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body))
    if gzip.decompress(blob) == html():
        print(f"{OUT.name} matches {SRC.name}")
        return 0
    print(f"{OUT.name} is out of date: run python tools/gzip_webui.py")
    return 1


def main() -> None:
    raw = html()
    blob = gzip.compress(raw, compresslevel=9, mtime=0)

    lines = [HEADER]
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    lines.append("};\n")
    lines.append("static const size_t WEBUI_HTML_GZ_LEN = sizeof(WEBUI_HTML_GZ);\n")

    OUT.write_text("".join(lines), encoding="utf-8", newline="\n")
    print(f"{SRC.name}: {len(raw)} B -> gzip {len(blob)} B "
          f"({100 * len(blob) / len(raw):.1f}%) -> {OUT.name}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        sys.exit(check())
    main()
