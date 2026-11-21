#!/usr/bin/env python3
"""Generate the rain radar's colour tables from RainViewer's published scheme.

    python3 tools/make_rain_palette.py [rainviewer_api_colors_table.csv]

Without an argument the CSV is downloaded from RainViewer
(https://www.rainviewer.com/files/rainviewer_api_colors_table.csv, linked from
https://www.rainviewer.com/api/color-schemes.html). The "Universal Blue"
column (colour scheme 2, the one the firmware requests) lists the RGBA value
of every rain and snow level; radar tiles contain exactly these values (every
visible pixel of 16 live tiles checked, 2026-11-17).

Writes two headers (do not edit them by hand):

  src/features/rain/rain_palette.h  the display palette: every colour of the
      scheme premultiplied on black (the screen's background) and reduced to
      what the RGB565 panel can show, sorted dark to light, index 0 = black.
      A frame on LittleFS is one byte per pixel into this palette.

  src/features/rain/rain_clut.h  what the decoder needs, in flash:
      - kRainSrc: the scheme's distinct RGBA values. The decoder's LZ window
        stores one byte per pixel, the pixel's index in this table, so a
        deflate copy rebuilds the exact bytes of its source.
      - kRainSrcHash: RGBA -> kRainSrc index (open addressing, 0xFF = empty).
      - kRainClut: premultiplied RGB565 -> nearest display colour, keyed on
        R5 G6 B4 (32 KB). G keeps 6 bits and B 4: coarser keys merge whole
        groups of the scheme's dark and blue levels.
"""
import os
import sys
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CSV_URL = "https://www.rainviewer.com/files/rainviewer_api_colors_table.csv"
COLUMN = "Universal Blue"
PALETTE_VERSION = 2          # bump when the meaning of a frame byte changes
HASH_BITS = 9


def load_csv(path):
    if path:
        text = open(path, encoding="utf-8").read()
    else:
        req = urllib.request.Request(CSV_URL, headers={"User-Agent": "Mozilla/5.0 (smalltv-mod tools)"})
        text = urllib.request.urlopen(req, timeout=60).read().decode("utf-8")
    rows = [line.split(",") for line in text.strip().splitlines()]
    col = rows[0].index(COLUMN)
    out = []
    for r in rows[1:]:
        h = r[col].strip().lstrip("#")
        out.append(tuple(int(h[i:i + 2], 16) for i in (0, 2, 4, 6)))
    assert len(out) == 256, f"expected 256 levels (rain + snow), got {len(out)}"
    return out


def premul(c):
    r, g, b, a = c
    f = lambda x: (x * a + 1 + (x * a >> 8)) >> 8   # same rounding as the firmware
    return f(r), f(g), f(b)


def to565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def expand565(v):
    """The 8-bit colour a RGB565 value shows (the panel's own expansion)."""
    r5, g6, b5 = v >> 11, (v >> 5) & 63, v & 31
    return (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)


def clut_key(v):
    return ((v >> 11) << 10) | (((v >> 5) & 63) << 4) | ((v & 31) >> 1)


def hash_rgba(c, m):
    r, g, b, a = c
    return ((r * m[0]) ^ (g * m[1]) ^ (b * m[2]) ^ (a * m[3])) & ((1 << HASH_BITS) - 1)


def build_hash(src):
    """Pick multipliers giving the shortest probe chains (deterministic search)."""
    size = 1 << HASH_BITS
    best = None
    odd = [m for m in range(3, 200, 2)]
    for m0 in odd[:24]:
        for m1 in odd[24:48]:
            m = (m0, m1, 37, 101)
            table = [0xFF] * size
            longest = 0
            for i, c in enumerate(src):
                h, probes = hash_rgba(c, m), 1
                while table[h] != 0xFF:
                    h, probes = (h + 1) & (size - 1), probes + 1
                table[h] = i
                longest = max(longest, probes)
            if best is None or longest < best[0]:
                best = (longest, m, table)
    return best


def c_array(name, ctype, values, per_line=16, fmt=str):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("  " + ",".join(fmt(v) for v in values[i:i + per_line]) + ",")
    return f"static const {ctype} {name} PROGMEM = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    levels = load_csv(sys.argv[1] if len(sys.argv) > 1 else None)

    # Source table: distinct RGBA in table order, the empty level first.
    src = []
    for c in levels:
        if c not in src:
            src.append(c)
    assert src[0] == (0, 0, 0, 0), "the scheme's first level should be transparent"
    assert len(src) < 255, "kRainSrc must fit a byte with 0xFF left for 'empty'"

    # Display palette: premultiplied, as RGB565, unique, dark -> light.
    disp565 = sorted({to565(premul(c)) for c in src}, key=lambda v: (sum(expand565(v)), v))
    assert disp565[0] == 0, "index 0 must stay black"
    assert len(disp565) <= 255
    disp_rgb = [expand565(v) for v in disp565]

    # LUT: nearest display colour to each bin centre, then every palette
    # colour maps to itself (where two share a bin, the darker one keeps it).
    lut = []
    for key in range(1 << 15):
        r5, g6, b4 = key >> 10, (key >> 4) & 63, key & 15
        centre = ((r5 << 3) | 4, (g6 << 2) | 2, (b4 << 4) | 8)
        best, bestd = 0, 1 << 30
        for k, (cr, cg, cb) in enumerate(disp_rgb):
            d = (centre[0] - cr) ** 2 + (centre[1] - cg) ** 2 + (centre[2] - cb) ** 2
            if d < bestd:
                best, bestd = k, d
        lut.append(best)
    for k in range(len(disp565) - 1, -1, -1):
        lut[clut_key(disp565[k])] = k
    shared = len(disp565) - len({clut_key(v) for v in disp565})

    longest, mult, table = build_hash(src)

    source_note = ("// Source: RainViewer's published colour table, scheme 2 \"Universal Blue\"\n"
                   f"// ({CSV_URL}).\n")
    pal = (
        "// rain_palette.h — generated by tools/make_rain_palette.py, do not edit.\n"
        + source_note +
        "// Display palette: every colour of the scheme (rain and snow levels)\n"
        "// premultiplied on black and reduced to RGB565, sorted dark to light;\n"
        "// index 0 = black (no echo). A frame byte is an index into this table.\n"
        "#pragma once\n#include <Arduino.h>\n\n"
        f"// Bumped when the meaning of a frame byte changes: frames stored under\n"
        f"// another version are dropped and fetched again.\n"
        f"#define RAIN_PALETTE_VERSION {PALETTE_VERSION}\n"
        f"#define RAIN_COLOR_COUNT {len(disp565)}\n"
        + c_array("kRainColors[RAIN_COLOR_COUNT][3]", "uint8_t", disp_rgb, 6,
                  lambda c: "{%d,%d,%d}" % c)
    )
    clut = (
        "// rain_clut.h — generated by tools/make_rain_palette.py, do not edit.\n"
        + source_note +
        "// Decoder tables, in flash. Included by RainPng.cpp only.\n"
        "#pragma once\n#include <Arduino.h>\n#include \"rain_palette.h\"\n\n"
        f"// The scheme's distinct RGBA values; the LZ window stores an index here.\n"
        f"#define RAIN_SRC_COUNT {len(src)}\n"
        + c_array("kRainSrc[RAIN_SRC_COUNT][4]", "uint8_t", src, 4, lambda c: "{%d,%d,%d,%d}" % c) +
        f"\n// RGBA -> kRainSrc index: open addressing, linear probing, 0xFF = empty.\n"
        f"// Longest probe chain: {longest}.\n"
        f"#define RAIN_SRC_HASH_BITS {HASH_BITS}\n"
        f"static inline uint16_t rainSrcHash(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {{\n"
        f"  return (uint16_t)(((r * {mult[0]}u) ^ (g * {mult[1]}u) ^ (b * {mult[2]}u) ^ (a * {mult[3]}u)) &"
        f" ((1u << RAIN_SRC_HASH_BITS) - 1));\n}}\n"
        + c_array("kRainSrcHash[1 << RAIN_SRC_HASH_BITS]", "uint8_t", table, 32) +
        f"\n// Premultiplied RGB565 -> display palette index, keyed R5 G6 B4: the\n"
        f"// nearest palette colour to the key's centre, and each palette colour to\n"
        f"// itself (the darkest one where several share a key: {shared} here).\n"
        f"// In flash on the ESP8266 only. The ESP32 images have RAM to spare but\n"
        f"// little room left in their 1.5 MB app slot: RainPng.cpp fills a heap\n"
        f"// copy during each decode with the same rule (tests/rain checks that\n"
        f"// both give the same index for every RGB565 value).\n"
        f"#ifndef RAIN_CLUT_IN_FLASH\n#if defined(SMALLTV_ESP8266)\n#define RAIN_CLUT_IN_FLASH 1\n"
        f"#else\n#define RAIN_CLUT_IN_FLASH 0\n#endif\n#endif\n"
        f"#define RAIN_CLUT_KEYS (1u << 15)\n"
        f"#if RAIN_CLUT_IN_FLASH\n"
        + c_array("kRainClut[RAIN_CLUT_KEYS]", "uint8_t", lut, 32)
        + "#endif\n"
    )
    d = os.path.join(ROOT, "src", "features", "rain")
    open(os.path.join(d, "rain_palette.h"), "w").write(pal)
    open(os.path.join(d, "rain_clut.h"), "w").write(clut)
    print(f"source colours {len(src)}, display colours {len(disp565)}, "
          f"hash probes <= {longest} (multipliers {mult}), palette colours sharing a LUT key: {shared}")


if __name__ == "__main__":
    main()
