#!/usr/bin/env python3
"""Compiles a TrueType font into an STF font for a spec 2 theme (#31).

Pure Python with its own outline reader and rasterizer, so the same font gives
the same bytes on every computer: no FreeType or Pillow version decides a
pixel. Only TrueType (glyf) outlines are read, without hinting or kerning.
Usage: python tools/theme_font.py FONT.ttf OUTPUT.stf --size 40 --glyphs 0123456789:
"""
import argparse
import math
from pathlib import Path
import struct

MAX_HEIGHT = 96
MAX_ADVANCE = 120
MAX_BITMAPS = 65535


class TrueType:
    """The tables of a .ttf that outlines and metrics need."""

    def __init__(self, data):
        if data[:4] not in (b'\x00\x01\x00\x00', b'true'):
            raise ValueError('expected a TrueType font (.ttf) with glyf outlines')
        self.tables = {}
        try:
            for i in range(struct.unpack_from('>H', data, 4)[0]):
                tag, _, offset, length = struct.unpack_from('>4sIII', data, 12 + 16 * i)
                if offset + length > len(data):
                    raise ValueError('truncated font')
                self.tables[tag.decode('latin-1')] = data[offset:offset + length]
            for tag in ('head', 'hhea', 'maxp', 'cmap', 'hmtx', 'loca', 'glyf'):
                if tag not in self.tables:
                    raise ValueError(f'font has no {tag} table')
            head, hhea = self.tables['head'], self.tables['hhea']
            self.units = struct.unpack_from('>H', head, 18)[0]
            self.ascender, self.descender = struct.unpack_from('>hh', hhea, 4)
            metrics = struct.unpack_from('>H', hhea, 34)[0]
            count = struct.unpack_from('>H', self.tables['maxp'], 4)[0]
            if struct.unpack_from('>h', head, 50)[0]:
                self.loca = struct.unpack_from('>%dI' % (count + 1), self.tables['loca'])
            else:
                self.loca = [2 * v for v in struct.unpack_from('>%dH' % (count + 1), self.tables['loca'])]
            self.advances = [struct.unpack_from('>H', self.tables['hmtx'], 4 * min(i, metrics - 1))[0]
                             for i in range(count)]
            self.cmap = self._cmap()
        except struct.error as error:
            raise ValueError('truncated font') from error
        if not self.units or not metrics:
            raise ValueError('invalid font header')

    def _cmap(self):
        """The format 4 (Unicode BMP) subtable, as code point -> glyph."""
        cmap = self.tables['cmap']
        for i in range(struct.unpack_from('>H', cmap, 2)[0]):
            platform, encoding, offset = struct.unpack_from('>HHI', cmap, 4 + 8 * i)
            if (platform, encoding) not in ((3, 1), (0, 3), (0, 4)) or struct.unpack_from('>H', cmap, offset)[0] != 4:
                continue
            segments = struct.unpack_from('>H', cmap, offset + 6)[0] // 2
            ends = struct.unpack_from('>%dH' % segments, cmap, offset + 14)
            starts = struct.unpack_from('>%dH' % segments, cmap, offset + 16 + 2 * segments)
            deltas = struct.unpack_from('>%dH' % segments, cmap, offset + 16 + 4 * segments)
            ranges_at = offset + 16 + 6 * segments
            ranges = struct.unpack_from('>%dH' % segments, cmap, ranges_at)

            def glyph(code):
                for k in range(segments):
                    if starts[k] <= code <= ends[k]:
                        if not ranges[k]:
                            return (code + deltas[k]) & 0xffff
                        g = struct.unpack_from('>H', cmap, ranges_at + 2 * k + ranges[k] + 2 * (code - starts[k]))[0]
                        return (g + deltas[k]) & 0xffff if g else 0
                return 0
            return glyph
        raise ValueError('font has no Unicode (format 4) character map')

    def contours(self, index, depth=0):
        """Glyph `index` as contours of (x, y, on-curve) points, in font units."""
        if depth > 8:
            raise ValueError('composite glyphs nested too deep')
        glyf = self.tables['glyf'][self.loca[index]:self.loca[index + 1]]
        if not glyf:
            return []
        count = struct.unpack_from('>h', glyf, 0)[0]
        if count < 0:
            return self._composite(glyf, depth)
        ends = struct.unpack_from('>%dH' % count, glyf, 10)
        at = 12 + 2 * count + struct.unpack_from('>H', glyf, 10 + 2 * count)[0]
        points = ends[-1] + 1 if count else 0
        flags = []
        while len(flags) < points:
            flag = glyf[at]
            at += 1
            repeat = 1
            if flag & 8:
                repeat += glyf[at]
                at += 1
            flags.extend([flag] * repeat)
        coordinates = []
        for short, same in ((2, 16), (4, 32)):
            value, values = 0, []
            for flag in flags[:points]:
                if flag & short:
                    value += glyf[at] if flag & same else -glyf[at]
                    at += 1
                elif not flag & same:
                    value += struct.unpack_from('>h', glyf, at)[0]
                    at += 2
                values.append(value)
            coordinates.append(values)
        result, start = [], 0
        for end in ends:
            result.append([(coordinates[0][i], coordinates[1][i], bool(flags[i] & 1)) for i in range(start, end + 1)])
            start = end + 1
        return result

    def _composite(self, glyf, depth):
        result, at, more = [], 10, True
        while more:
            flags, index = struct.unpack_from('>HH', glyf, at)
            at += 4
            if not flags & 2:
                raise ValueError('composite glyphs placed by points are not supported')
            if flags & 1:
                dx, dy = struct.unpack_from('>hh', glyf, at)
                at += 4
            else:
                dx, dy = struct.unpack_from('>bb', glyf, at)
                at += 2
            a, b, c, d = 1.0, 0.0, 0.0, 1.0
            if flags & 8:
                a = d = struct.unpack_from('>h', glyf, at)[0] / 16384
                at += 2
            elif flags & 0x40:
                a, d = (v / 16384 for v in struct.unpack_from('>hh', glyf, at))
                at += 4
            elif flags & 0x80:
                a, b, c, d = (v / 16384 for v in struct.unpack_from('>hhhh', glyf, at))
                at += 8
            for contour in self.contours(index, depth + 1):
                result.append([(a * x + c * y + dx, b * x + d * y + dy, on) for x, y, on in contour])
            more = flags & 0x20
        return result


def outline(contours, scale, origin_x, origin_y):
    """The contours in pixels (y down, from `origin`), as line segments; each
    quadratic curve is split finely enough to stay within 0.05 px of it."""
    lines = []

    def point(x, y):
        return (x * scale - origin_x, origin_y - y * scale)

    for contour in contours:
        points = [point(x, y) + (on,) for x, y, on in contour]
        if not points:
            continue
        # Start on a point on the curve, adding the implied one between two
        # control points when there is none.
        first = next((i for i, p in enumerate(points) if p[2]), None)
        if first is None:
            a, b = points[0], points[1 % len(points)]
            start = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        else:
            start = points[first][:2]
            points = points[first + 1:] + points[:first + 1]
        current, control = start, None
        for x, y, on in points + [start + (True,)]:
            if on:
                if control is None:
                    lines.append((current, (x, y)))
                else:
                    lines.extend(curve(current, control, (x, y)))
                current, control = (x, y), None
            elif control is None:
                control = (x, y)
            else:
                middle = ((control[0] + x) / 2, (control[1] + y) / 2)
                lines.extend(curve(current, control, middle))
                current, control = middle, (x, y)
    return lines


def curve(p0, p1, p2):
    dx, dy = p0[0] - 2 * p1[0] + p2[0], p0[1] - 2 * p1[1] + p2[1]
    steps = max(1, min(64, math.ceil(math.sqrt(math.sqrt(dx * dx + dy * dy) / 0.4))))
    result, previous = [], p0
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        point = (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])
        result.append((previous, point))
        previous = point
    return result


def rasterize(lines, width, height):
    """Exact area coverage (0-1) of each pixel inside the outline: each edge
    adds its signed area to an accumulation buffer, whose running sum is the
    coverage (the method of font-rs and stb_truetype)."""
    acc = [0.0] * (width * height + width + 2)
    for (x0, y0), (x1, y1) in lines:
        # Points lie inside the buffer; this only keeps a rounding error
        # (a curve point at -1e-17) from indexing outside it.
        x0, x1 = max(0.0, x0), max(0.0, x1)
        if y0 == y1:
            continue
        direction = 1.0
        if y0 > y1:
            direction, x0, y0, x1, y1 = -1.0, x1, y1, x0, y0
        dxdy = (x1 - x0) / (y1 - y0)
        x = x0
        if y0 < 0:
            x -= y0 * dxdy
            y0 = 0.0
        for y in range(int(y0), min(height, math.ceil(y1))):
            start = y * width
            dy = min(y + 1.0, y1) - max(float(y), y0)
            xnext = x + dxdy * dy
            d = dy * direction
            left, right = (x, xnext) if x < xnext else (xnext, x)
            left_floor = math.floor(left)
            li, ri = int(left_floor), int(math.ceil(right))
            if ri <= li + 1:
                middle = 0.5 * (x + xnext) - left_floor
                acc[start + li] += d - d * middle
                acc[start + li + 1] += d * middle
            else:
                s = 1.0 / (right - left)
                lf = left - left_floor
                a0 = 0.5 * s * (1.0 - lf) * (1.0 - lf)
                rf = right - math.ceil(right) + 1.0
                am = 0.5 * s * rf * rf
                acc[start + li] += d * a0
                if ri == li + 2:
                    acc[start + li + 1] += d * (1.0 - a0 - am)
                else:
                    a1 = s * (1.5 - lf)
                    acc[start + li + 1] += d * (a1 - a0)
                    for xi in range(li + 2, ri - 1):
                        acc[start + xi] += d * s
                    a2 = a1 + (ri - li - 3) * s
                    acc[start + ri - 1] += d * (1.0 - a2 - am)
                acc[start + ri] += d * am
            x = xnext
    total, coverage = 0.0, []
    for i in range(width * height):
        total += acc[i]
        coverage.append(min(1.0, abs(total)))
    return [coverage[y * width:(y + 1) * width] for y in range(height)]


def bounds(contours):
    points = [(x, y) for contour in contours for x, y, _ in contour]
    if not points:
        return None
    return (min(x for x, _ in points), min(y for _, y in points), max(x for x, _ in points), max(y for _, y in points))


def compile_font(data, size, glyphs, fallback=None):
    """STF bytes for the characters in `glyphs` (printable ASCII) of the
    TrueType font `data`, at `size` pixels per em."""
    font = TrueType(data)
    if type(size) is not int or not 4 <= size <= 200:
        raise ValueError('size: expected an integer from 4 to 200 pixels per em')
    if not isinstance(glyphs, str) or not glyphs or any(not 32 <= ord(c) <= 126 for c in glyphs):
        raise ValueError('glyphs: expected printable ASCII characters')
    codes = sorted(set(map(ord, glyphs)))
    if fallback is None:
        fallback = '?' if '?' in glyphs else chr(codes[0])
    if not isinstance(fallback, str) or len(fallback) != 1 or ord(fallback) not in codes:
        raise ValueError('fallback: expected one of the glyphs')
    scale = size / font.units
    outlines = {}
    for code in codes:
        index = font.cmap(code)
        if not 0 < index < len(font.advances):
            raise ValueError(f'font has no glyph for {chr(code)!r}')
        try:
            outlines[code] = (index, font.contours(index))
        except (struct.error, IndexError) as error:
            raise ValueError(f'{chr(code)!r}: truncated glyph') from error
    # The line box fits the ink of the glyphs kept, so a layer's position and
    # anchor place what is drawn: digits alone get no room for descenders.
    # Only glyphs without ink fall back to the font's ascender and descender.
    boxes = [box for box in (bounds(contours) for _, contours in outlines.values()) if box]
    if boxes:
        ascent = max(math.ceil(box[3] * scale) for box in boxes)
        descent = max(0, max(math.ceil(-box[1] * scale) for box in boxes))
    else:
        ascent, descent = math.ceil(font.ascender * scale), math.ceil(-font.descender * scale)
    height = ascent + descent
    if not 6 <= height <= MAX_HEIGHT:
        raise ValueError(f'line height {height} px is outside 6-{MAX_HEIGHT}; change the size')
    records, bitmaps = [], bytearray()
    for code in range(codes[0], codes[-1] + 1):
        if code not in outlines:
            records.append(bytes(8))
            continue
        index, contours = outlines[code]
        advance = math.floor(font.advances[min(index, len(font.advances) - 1)] * scale + 0.5)
        box = bounds(contours)
        ink = []
        if box:
            left = math.floor(box[0] * scale)
            width = math.ceil(box[2] * scale) - left + 1
            coverage = rasterize(outline(contours, scale, left, ascent), width, height)
            levels = [[min(15, math.floor(c * 15 + 0.5)) for c in row] for row in coverage]
            rows = [y for y in range(height) if any(levels[y])]
            columns = [x for x in range(width) if any(levels[y][x] for y in range(height))]
            if rows:
                ink = [levels[y][columns[0]:columns[-1] + 1] for y in range(rows[0], rows[-1] + 1)]
                left, top = left + columns[0], rows[0]
        if not ink:
            if not 1 <= advance <= MAX_ADVANCE:
                raise ValueError(f'{chr(code)!r}: advance {advance} px is outside 1-{MAX_ADVANCE}')
            records.append(struct.pack('<HBBBBBx', 0, advance, 0, 0, 0, 0))
            continue
        # Ink left of the pen moves the glyph right, and ink past the advance
        # widens it: every glyph's ink lies inside its advance.
        if left < 0:
            advance -= left
            left = 0
        advance = max(advance, left + len(ink[0]))
        if advance > MAX_ADVANCE:
            raise ValueError(f'{chr(code)!r}: advance {advance} px exceeds {MAX_ADVANCE}; change the size')
        records.append(struct.pack('<HBBBBBx', len(bitmaps), advance, left, top, len(ink[0]), len(ink)))
        for row in ink:
            row = row + [0]
            bitmaps.extend(row[k] << 4 | row[k + 1] for k in range(0, len(row) - 1, 2))
        if len(bitmaps) > MAX_BITMAPS:
            raise ValueError(f'glyph bitmaps exceed {MAX_BITMAPS} bytes; use fewer glyphs or a smaller size')
    header = b'STF1' + bytes([height, ascent, codes[0], len(records), ord(fallback), 0, 0, 0])
    return header + b''.join(records) + bytes(bitmaps)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('font', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--size', type=int, required=True, help='pixels per em, 4-200')
    parser.add_argument('--glyphs', required=True, help='the characters to include, printable ASCII')
    parser.add_argument('--fallback', help="drawn for a character the font lacks (default: '?' when included, else the first)")
    args = parser.parse_args()
    try:
        data = compile_font(args.font.read_bytes(), args.size, args.glyphs, args.fallback)
        args.output.write_bytes(data)
    except (OSError, ValueError) as error:
        parser.exit(1, f'Font build failed: {error}\n')
    print(f'{args.output}: {len(data)} bytes, line height {data[4]} px')


if __name__ == '__main__':
    main()
