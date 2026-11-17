#!/usr/bin/env python3
"""Write a tile whose deflate stream opens with many empty blocks.

Takes a seed tile (a 256x256 RGBA PNG) and re-encodes its pixel data with
EMPTY fixed-Huffman blocks in front (or empty dynamic ones with --dynamic),
up to about the 256 KB download cap. The image is the same, so the decode
must give the same frame; it is the decoder's per-block cost that this
measures (review of PR #122: a table rebuilt in O(n^2) for every block, and
no yield(), kept the loop busy for a long time on such a tile).

    python3 tests/rain/make_slow_tile.py SEED.png OUT.png [--dynamic]

With --alternate the tile is instead a clear sky (every pixel transparent,
in RainViewer's scheme) written as ~77,000 three-byte copies that alternate
between 20,000 and 30,000 bytes back: in the small-memory mode, both
sources lie in stored history segments, and in different ones, so every
copy reads another segment (a file reopened on the device). Second review
of PR #122: minutes of decoding inside one call without a time budget.

    python3 tests/rain/make_slow_tile.py SEED.png OUT.png --alternate
"""
import struct
import sys
import zlib


def chunks(data):
    pos = 8
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        yield data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


class Bits:
    """LSB-first bit writer (deflate order)."""
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, count):
        self.acc |= value << self.n
        self.n += count
        while self.n >= 8:
            self.out.append(self.acc & 0xFF)
            self.acc >>= 8
            self.n -= 8

    def put_code(self, code, length):
        """A Huffman code, sent MSB-first."""
        for i in range(length - 1, -1, -1):
            self.put((code >> i) & 1, 1)


def empty_fixed(b):
    b.put(0, 1)          # BFINAL = 0
    b.put(1, 2)          # BTYPE = 01, fixed
    b.put_code(0, 7)     # end of block: symbol 256 = 7-bit code 0000000


def empty_dynamic(b):
    # HLIT = 257 (all literal/length codes 0 except 256), HDIST = 1, HCLEN = 18:
    # code-length alphabet with two 1-bit codes, for "0" and "18"; then
    # 256 zeros (18 x 138 + 18 x 118), a "1" is not available, so the
    # end-of-block code gets length... keep it simple: lengths {0:1, 1:1}.
    b.put(0, 1)
    b.put(2, 2)          # BTYPE = 10, dynamic
    b.put(0, 5)          # HLIT - 257
    b.put(0, 5)          # HDIST - 1
    b.put(14, 4)         # HCLEN - 4 = 14 -> 18 code-length lengths in the order below
    order = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1]
    lens = {18: 2, 0: 2, 1: 1}   # code lengths of the code-length alphabet: complete code
    for sym in order:
        b.put(lens.get(sym, 0), 3)
    # Canonical codes: length 1: sym 1 -> 0; length 2: sym 0 -> 10, sym 18 -> 11.
    def cl(sym):
        return {1: (0, 1), 0: (2, 2), 18: (3, 2)}[sym]
    # 256 zeros for literals 0..255: 18 (11 + 7 bits) x 2 = 138 + 118
    for run in (138, 118):
        b.put_code(*cl(18))
        b.put(run - 11, 7)
    b.put_code(*cl(1))   # symbol 256: length 1 (the only literal/length code)
    b.put_code(*cl(1))   # distance 0: length 1
    b.put_code(0, 1)     # end of block: the single 1-bit code


def fixed_length(b, length):
    """Length code for 3 or 258 only (the two this file uses), fixed Huffman."""
    if length == 3:
        b.put_code(1, 7)             # symbol 257: 7-bit code 0000001
    else:
        b.put_code(0xC5, 8)          # symbol 285 (length 258): 8-bit code 11000101


def fixed_distance(b, dist):
    """Distance 1, or 16,385..32,768 (codes 28 and 29, 13 extra bits)."""
    if dist == 1:
        b.put_code(0, 5)
    elif dist <= 24576:
        b.put_code(28, 5)
        b.put(dist - 16385, 13)
    else:
        b.put_code(29, 5)
        b.put(dist - 24577, 13)


def alternate_stream(raw_len):
    """A fixed-Huffman stream that inflates to raw_len zero bytes."""
    b = Bits()
    b.put(1, 1)                      # BFINAL = 1
    b.put(1, 2)                      # fixed Huffman
    b.put_code(0x30, 8)              # literal 0
    out = 1
    while out < 30000:               # zeros by long copies, one byte back
        fixed_length(b, 258)
        fixed_distance(b, 1)
        out += 258
    copies = 0
    while raw_len - out >= 3:
        fixed_length(b, 3)
        fixed_distance(b, 20000 if copies % 2 == 0 else 30000)
        out += 3
        copies += 1
    while out < raw_len:             # the last bytes as literals
        b.put_code(0x30, 8)
        out += 1
    b.put_code(0, 7)                 # end of block
    b.put(0, (8 - b.n) % 8)
    return bytes(b.out), copies


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    ihdr = None
    idat = b""
    for kind, body in chunks(data):
        if kind == b"IHDR":
            ihdr = body
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    if "--alternate" in sys.argv[3:]:
        raw = bytes(len(raw))        # clear sky: filter bytes and pixels all 0
        body, copies = alternate_stream(len(raw))
        stream = b"\x78\x01" + body + struct.pack(">I", zlib.adler32(raw) & 0xFFFFFFFF)
        assert zlib.decompress(stream) == raw, "the alternating stream does not decode to a clear sky"
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", stream) + chunk(b"IEND", b"")
        open(sys.argv[2], "wb").write(png)
        print(f"{copies} alternating copies, {len(png)} bytes")
        return
    dynamic = "--dynamic" in sys.argv[3:]
    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    body = co.compress(raw) + co.flush()
    budget = 250_000 - len(body)     # stay under the firmware's 256 KB download cap
    b = Bits()
    blocks = 0
    while len(b.out) < budget:
        (empty_dynamic if dynamic else empty_fixed)(b)
        blocks += 1
    while b.n:                       # pad with empty blocks to a byte boundary
        (empty_dynamic if dynamic else empty_fixed)(b)
        blocks += 1
    stream = b"\x78\x01" + bytes(b.out) + body + struct.pack(">I", zlib.adler32(raw) & 0xFFFFFFFF)
    assert zlib.decompress(stream) == raw, "the re-encoded stream does not decode to the same pixels"
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", stream) + chunk(b"IEND", b"")
    open(sys.argv[2], "wb").write(png)
    print(f"{blocks} empty {'dynamic' if dynamic else 'fixed'} blocks, {len(png)} bytes")


if __name__ == "__main__":
    main()
