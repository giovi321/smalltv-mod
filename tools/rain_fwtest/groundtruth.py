#!/usr/bin/env python3
# groundtruth.py — vérité terrain pour le décodeur rain (RainPng.cpp)
#
# Décoder la tuile avec zlib (filtres None vérifiés), prémultiplier l'alpha
# sur noir, cropper le centre KxK, upscaler en bilinéaire 8.8 fixe 240x240
# (réplique exacte de emitPair/emitRow/rainColorLutBuild), quantifier via la
# même LUT que le firmware (make_rain_clut.py, idempotence incluse), et
# comparer aux indices du .raw produit par fwtest : tolérance ±12/canal sur
# les couleurs kRainColors.
#
# Usage : python3 groundtruth.py tuile.png sortie.raw 120
import re, struct, sys, zlib

# kRainColors lu depuis RainData.h (source unique de vérité)
_HDR = open("../../src/features/rain/RainData.h").read()
PAL = [tuple(int(v) for v in m) for m in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", _HDR)]
N = len(PAL)


def lut():
    # réplique de tools/make_rain_clut.py : plus proche voisin du centre du
    # bin (R5/G5 couvrent 8 niveaux, B2 couvre 64), puis idempotence forcée
    # en ordre inverse (la couleur la plus sombre gagne un bin partagé).
    table = [0] * 4096
    for i in range(4096):
        r5 = (i >> 7) & 31
        g5 = (i >> 2) & 31
        b2 = i & 3
        r, g, b = r5 * 8 + 4, g5 * 8 + 4, b2 * 64 + 32
        best, bestd = 0, 1 << 30
        for k, (cr, cg, cb) in enumerate(PAL):
            d = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
            if d < bestd:
                bestd, best = d, k
        table[i] = best
    for k in range(N - 1, -1, -1):
        r, g, b = PAL[k]
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        key = ((c >> 11) << 7) | ((((c >> 5) & 63) >> 1) << 2) | ((c & 31) >> 3)
        table[key] = k
    return table


def idx_of(rgb565):
    if rgb565 == 0:
        return 0   # raccourci du firmware : transparent = noir
    c = rgb565
    key = ((c >> 11) << 7) | ((((c >> 5) & 63) >> 1) << 2) | ((c & 31) >> 3)
    return LUT[key]


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
        assert row[0] == 0, f"filtre != None ligne {y}"
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
    # upscale : réplique de x0tab/txTab/emitPair/emitRow (8.8 fixe)
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
    print(f"pixels: {tot}, indices differents: {badidx} ({100.0 * badidx / tot:.3f}%), "
          f">±12/canal: {bad12} ({100.0 * bad12 / tot:.3f}%), ecart max: {maxd}")


LUT = lut()
main()
