#!/usr/bin/env python3
"""Write the seed tiles of the `rain` fuzz target to tests/fuzz/corpus/rain/.

The tiles are synthetic (no RainViewer image is stored in the repository):
256x256 RGBA PNGs, filter "None" on every row, like the radar tiles, with
rain-like blobs in the RGBA values of RainViewer's colour scheme
(kRainSrc), as real tiles hold them, and one tile off the scheme. Each seed reaches a different
part of the decoder: dynamic, fixed and stored deflate blocks, literal-only
and run-length streams (copies at distance 1, so a copied byte lands on
another channel or on a row's filter byte), several IDAT chunks, a chunk
before IHDR is read, and colours outside the scheme (the decoder's
nearest-colour fallback).

Deterministic: the same files on every run.
    python3 tests/fuzz/make_rain_seeds.py
"""
import os
import re
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
OUT = os.path.join(ROOT, "tests", "fuzz", "corpus", "rain")
# The colours of RainViewer's scheme, as the decoder knows them (kRainSrc in
# the generated rain_clut.h): the visible ones, in table order.
_CLUT = open(os.path.join(ROOT, "src", "features", "rain", "rain_clut.h")).read()
SRC = [tuple(int(v) for v in m) for m in re.findall(
    r"\{(\d+),(\d+),(\d+),(\d+)\}", _CLUT[_CLUT.index("kRainSrc["):_CLUT.index("};", _CLUT.index("kRainSrc["))])]
PAL = [c for c in SRC if c[3] > 0]


class Rng:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & (2**64 - 1)
        return self.s >> 33

    def below(self, n):
        return self.next() % n


def field(seed, alpha_mode, count=12, size=50):
    """Rows of RGBA bytes: blobs of palette colours on a transparent sky."""
    rng = Rng(seed)
    blobs = [(rng.below(256), rng.below(256), 6 + rng.below(size), 1 + rng.below(len(PAL) - 1))
             for _ in range(count)]
    rows = []
    for y in range(256):
        row = bytearray()
        for x in range(256):
            px = (0, 0, 0, 0)
            for bx, by, r, k in blobs:
                d2 = (x - bx) ** 2 + (y - by) ** 2
                if d2 < r * r:
                    ring = min(len(PAL) - 1, k + (r * r - d2) * 4 // (r * r))
                    if alpha_mode == "scheme":     # the scheme's own RGBA, as in real tiles
                        px = PAL[ring]
                    else:   # off the scheme: its RGB with any alpha (the decoder's fallback)
                        a = max(1, min(255, 40 + (r * r - d2) * 215 // (r * r)))
                        px = PAL[ring][:3] + (a,)
            row += bytes(px)
        rows.append(bytes(row))
    return rows


def raw_of(rows):
    return b"".join(b"\x00" + r for r in rows)


def deflate(data, level=9, strategy=zlib.Z_DEFAULT_STRATEGY):
    c = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
    return c.compress(data) + c.flush()


def stored_then_dynamic(data, head):
    """zlib stream: the first `head` bytes in stored blocks, the rest dynamic."""
    a = zlib.compressobj(0, zlib.DEFLATED, -15)
    part = a.compress(data[:head]) + a.flush(zlib.Z_FULL_FLUSH)
    b = zlib.compressobj(9, zlib.DEFLATED, -15)
    part += b.compress(data[head:]) + b.flush()
    return b"\x78\x01" + part + struct.pack(">I", zlib.adler32(data))


def chunk(typ, body):
    return struct.pack(">I", len(body)) + typ + body + struct.pack(">I", zlib.crc32(typ + body))


def png(z, idat_parts=1, extra_before=False):
    ihdr = struct.pack(">IIBBBBB", 256, 256, 8, 6, 0, 0, 0)
    out = b"\x89PNG\r\n\x1a\n"
    if extra_before:
        out += chunk(b"tEXt", b"Comment\x00rain fuzz seed")
    out += chunk(b"IHDR", ihdr)
    step = (len(z) + idat_parts - 1) // idat_parts
    for i in range(0, len(z), step):
        out += chunk(b"IDAT", z[i:i + step])
    return out + chunk(b"IEND", b"")


def main():
    os.makedirs(OUT, exist_ok=True)
    rain = raw_of(field(1, "scheme"))
    offscheme = raw_of(field(2, "offscheme", count=4, size=20))
    sparse = raw_of(field(3, "scheme", count=6, size=20)[:60] + [bytes(1024)] * 196)   # mostly empty sky
    seeds = {
        "dynamic.png": png(deflate(rain)),
        "fixed.png": png(deflate(rain, 6, zlib.Z_FIXED)),
        "rle-sky.png": png(deflate(sparse, 9, zlib.Z_RLE)),
        "stored-head.png": png(stored_then_dynamic(rain, 3 * 1025)),
        "split-idat.png": png(deflate(rain, 9), idat_parts=5, extra_before=True),
        "offscheme-fast.png": png(deflate(offscheme, 1)),
    }
    for name, data in seeds.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} B")


if __name__ == "__main__":
    main()
