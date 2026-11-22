#!/usr/bin/env python3
# groundtruth.py — zlib reference for the rain decoder (RainPng.cpp)
#
# Decodes the tile with zlib (filter None checked), premultiplies the alpha
# on black, crops the centre KxK, upscales to 240x240 with the 8.8
# fixed-point bilinear filter (an exact replica of emitPair/emitRow),
# quantizes through the same lookup as the firmware (rain_clut.h, generated
# by tools/make_rain_palette.py), and compares with the indices of the .raw
# written by fwtest, counting pixels more than 12 per channel off in
# kRainColors.
#
# Usage: python3 groundtruth.py tile.png out.raw 120
import re, struct, sys, zlib

# Palette and lookup read from the generated headers (tools/make_rain_palette.py),
# the single source of truth shared with the firmware.
def _array(text, name):
    body = text[text.index(name):]
    body = body[body.index("{") + 1:body.index("};")]
    return [int(v) for v in re.findall(r"\d+", body)]


_PAL = open("../../src/features/rain/rain_palette.h").read()
_CLUT = open("../../src/features/rain/rain_clut.h").read()
_flat = _array(_PAL, "kRainColors[")
PAL = [tuple(_flat[i:i + 3]) for i in range(0, len(_flat), 3)]
N = len(PAL)
CLUT = _array(_CLUT, "kRainClut[")
assert len(CLUT) == 1 << 15, len(CLUT)


def idx_of(rgb565):
    if rgb565 == 0:
        return 0   # firmware shortcut: transparent = black
    c = rgb565
    return CLUT[((c >> 11) << 10) | (((c >> 5) & 63) << 4) | ((c & 31) >> 1)]


def premul(r, g, b, a):
    rr = (r * a + 1 + (r * a >> 8)) >> 8
    gg = (g * a + 1 + (g * a >> 8)) >> 8
    bb = (b * a + 1 + (b * a >> 8)) >> 8
    return rr, gg, bb


def main():
    png = open(sys.argv[1], "rb").read()
    raw = open(sys.argv[2], "rb").read()
    K = int(sys.argv[3])
    assert len(raw) == 240 * 240, len(raw)
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat = 8, b""
    while pos < len(png):
        ln = struct.unpack(">I", png[pos:pos + 4])[0]
        typ = png[pos + 4:pos + 8]
        if typ == b"IHDR":
            W, H, bpp, ctype, _, _, inter = struct.unpack(">IIBBBBB", png[pos + 8:pos + 21])
            assert (W, H, bpp, ctype, inter) == (256, 256, 8, 6, 0)
        elif typ == b"IDAT":
            idat += png[pos + 8:pos + 8 + ln]
        elif typ == b"IEND":
            break
        pos += 12 + ln
    data = zlib.decompress(idat)
    assert len(data) == 256 * (1 + 256 * 4), len(data)
    src = []
    for y in range(256):
        row = data[y * 1025:(y + 1) * 1025]
        assert row[0] == 0, f"filter != None on row {y}"
        src.append(row[1:])
    cx0 = (256 - K) // 2
    crop = []
    for y in range(cx0, cx0 + K):
        r = []
        for x in range(cx0, cx0 + K):
            p = src[y][x * 4:x * 4 + 4]
            pr, pg, pb = premul(p[0], p[1], p[2], p[3])
            r.append((pr >> 3) << 11 | (pg >> 2) << 5 | (pb >> 3))
        crop.append(r)
    # upscale: replica of x0tab/txTab/emitPair/emitRow (8.8 fixed point)
    A = K - 1
    x0t, txt = [], []
    for x in range(240):
        fx = x * A * 256 // 239
        x0t.append(fx >> 8)
        txt.append(fx & 255)

    def bilin(p00, p10, p01, p11, tx, ty):
        w00 = (256 - tx) * (256 - ty)
        w10 = tx * (256 - ty)
        w01 = (256 - tx) * ty
        w11 = tx * ty
        r = ((p00 >> 11) * w00 + (p10 >> 11) * w10 + (p01 >> 11) * w01 + (p11 >> 11) * w11 + 32768) >> 16
        g = (((p00 >> 5) & 63) * w00 + ((p10 >> 5) & 63) * w10 + ((p01 >> 5) & 63) * w01 + ((p11 >> 5) & 63) * w11 + 32768) >> 16
        b = ((p00 & 31) * w00 + (p10 & 31) * w10 + (p01 & 31) * w01 + (p11 & 31) * w11 + 32768) >> 16
        return (r << 11) | (g << 5) | b

    bad12 = badidx = 0
    maxd = 0
    for oy in range(240):
        for x in range(240):
            x0 = x0t[x]
            x1 = x0 + 1 if x0 + 1 < K else x0
            tx = txt[x]
            if oy < 239:
                r = min((oy * A) // 239, K - 2)
                ty = (oy * A * 256 // 239) & 255
                c = bilin(crop[r][x0], crop[r][x1], crop[r + 1][x0], crop[r + 1][x1], tx, ty)
            else:
                c = bilin(crop[K - 1][x0], crop[K - 1][x1], crop[K - 1][x0], crop[K - 1][x1], tx, 0)
            gi = idx_of(c)
            fi = raw[oy * 240 + x]
            if gi != fi:
                badidx += 1
                pr, pg, pb = PAL[fi]
                gr, gg, gb = PAL[gi]
                d = max(abs(pr - gr), abs(pg - gg), abs(pb - gb))
                maxd = max(maxd, d)
                if d > 12:
                    bad12 += 1
    tot = 240 * 240
    print(f"pixels: {tot}, differing indices: {badidx} ({100.0 * badidx / tot:.3f}%), "
          f">12/channel: {bad12} ({100.0 * bad12 / tot:.3f}%), max diff: {maxd}")


main()
