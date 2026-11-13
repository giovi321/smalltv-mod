#!/usr/bin/env python3
# make_rain_clut.py — régénère src/features/rain/rain_clut.h
#
# La LUT R5G5B2 (4096 entrées) mappe un RGB565 prémultiplié vers l'indice
# kRainColors le plus proche. Elle est précalculée ici et stockée en flash
# (PROGMEM) : la construire au runtime coûtait 4 Ko de BSS, ce qui faisait
# échouer le malloc de la fenêtre LZ (8 Ko) sur l'ESP8266 (heap serré).
#
# Centres des bins : R5 et G5 couvrent 8 niveaux par pas (r5*8+4, g5*8+4 —
# la clé garde G5 = G6>>1), B2 couvre 64 niveaux (b2*64+32). Un centre faux
# (g5*4+2, b2*64+36 dans les premières versions) écartait la LUT des
# couleurs réelles — ne pas réintroduire.
#
# Usage : python3 tools/make_rain_clut.py [rain_data.h] [sortie]
# (défauts : src/features/rain/RainData.h -> src/features/rain/rain_clut.h)
import re, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "src/features/rain/RainData.h"
DST = sys.argv[2] if len(sys.argv) > 2 else "src/features/rain/rain_clut.h"

hdr = open(SRC).read()
rows = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", hdr)
pal = [(int(r), int(g), int(b)) for r, g, b in rows]
assert len(pal) == 58, f"attendu 58 couleurs, trouvé {len(pal)}"
assert pal[0] == (0, 0, 0), "index 0 doit rester noir/aucune pluie"

lut = []
for i in range(4096):
    r5 = (i >> 7) & 31
    g5 = (i >> 2) & 31
    b2 = i & 3
    r = r5 * 8 + 4
    g = g5 * 8 + 4      # G5 = G6>>1 : mêmes pas de 8 que R5
    b = b2 * 64 + 32
    best, bestd = 0, 1 << 30
    for k, (cr, cg, cb) in enumerate(pal):
        d = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
        if d < bestd:
            bestd, best = d, k
    lut.append(best)

# Idempotence : quantize(PAL[k]) == k pour toute couleur de la palette (sinon
# un pixel affiché avec la couleur k retombe sur un voisin à la re-quantifica-
# tion — dérive d'un pas sur les copies LZ et les pixels « tout-histoire »).
# Ordre inverse : dans un bin partagé par des couleurs quasi identiques, la
# plus sombre (plus petit indice) gagne — et le noir garde le bin 0.
for k in range(len(pal) - 1, -1, -1):
    r, g, b = pal[k]
    c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    key = ((c >> 11) << 7) | ((((c >> 5) & 63) >> 1) << 2) | ((c & 31) >> 3)
    lut[key] = k

lines = []
for off in range(0, 4096, 16):
    lines.append("  " + ",".join(f"{v:3d}" for v in lut[off:off + 16]) + ",")

out = f"""// rain_clut.h — GÉNÉRÉ par tools/make_rain_clut.py. NE PAS ÉDITER.
// LUT R5G5B2 (4096 entrées) : RGB565 prémultiplié -> indice kRainColors
// (plus proche voisin du centre du bin, ex æquo = plus petit indice).
// En flash (PROGMEM) pour économiser 4 Ko de BSS sur l'ESP8266.
#pragma once
#include <Arduino.h>

static const uint8_t kRainClut[4096] PROGMEM = {{
{chr(10).join(lines)}}};
"""
open(DST, "w").write(out)
print(f"{DST} : {len(lut)} entrées écrites")
