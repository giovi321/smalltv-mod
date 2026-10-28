#!/usr/bin/env python3
"""Bootstrap .stheme packer. PNG conversion runs on the desktop, never on ESP32.
Usage: python tools/theme_pack.py SOURCE_DIRECTORY OUTPUT.stheme
Requires Pillow and a desktop C++11 compiler. Uses the firmware validator locally.
"""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import struct
import tempfile
try:
    from .theme_native import validate_manifest, validate_package
except ImportError:
    from theme_native import validate_manifest, validate_package
from PIL import Image

MAX_PACKAGE = 3 * 1024 * 1024


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def image_rgba(image):
    w, h = image.size
    if not (1 <= w <= 240 and 1 <= h <= 240):
        raise ValueError('Image dimensions must be 1..240 pixels')
    rgba = image.convert('RGBA')
    return rgba, rgba.getextrema()[3][0] != 255


def encode_sti1(image):
    rgba, has_alpha = image_rgba(image)
    w, h = rgba.size
    data = bytearray(b'STI1' + struct.pack('<HHB3x', w, h, int(has_alpha)))
    for r, g, b, a in rgba.getdata():
        data.extend(struct.pack('<H', rgb565(r, g, b)))
        if has_alpha:
            data.append(a)
    return bytes(data)


def row_runs(row, min_repeat):
    """Splits a row into (kind, pixels) runs of at most 64 pixels: 0
    transparent (a pixel of None), 1 one pixel repeated, 2 literal pixels.
    A repeat is used from `min_repeat` equal pixels on."""
    runs, literal = [], []

    def add(kind, pixels):
        for start in range(0, len(pixels), 64):
            runs.append((kind, pixels[start:start + 64]))

    i = 0
    while i < len(row):
        j = i
        while j < len(row) and row[j] == row[i]:
            j += 1
        if row[i] is None or j - i >= min_repeat:
            add(2, literal)
            literal = []
            add(0 if row[i] is None else 1, row[i:j])
        else:
            literal.extend(row[i:j])
        i = j
    add(2, literal)
    return runs


def encode_sti2(image, indexed=False):
    """STI2 as agreed in #31: rows as runs, direct RGB565 or indexed through a
    palette of up to 16 colours (None when the image has more)."""
    rgba, has_alpha = image_rgba(image)
    w, h = rgba.size
    # A pixel is (RGB565, alpha), or None when fully transparent.
    pixels = [None if a == 0 else (rgb565(r, g, b), a) for r, g, b, a in rgba.getdata()]
    rows = [pixels[y * w:(y + 1) * w] for y in range(h)]
    palette = []
    if indexed:
        for pixel in pixels:
            if pixel is not None and pixel not in palette:
                palette.append(pixel)
        if len(palette) > 16:
            return None

    def colour(pixel):
        return struct.pack('<H', pixel[0]) + (bytes([pixel[1]]) if has_alpha else b'')

    def encode_row(row):
        data = bytearray()
        for kind, run in row_runs(row, 4 if indexed else 2):
            data.append(kind << 6 | (len(run) - 1))
            if kind == 1:
                data.extend(bytes([palette.index(run[0])]) if indexed else colour(run[0]))
            elif kind == 2 and indexed:
                # Two indices per byte, high nibble first; an odd run's
                # unused low nibble is 0.
                indices = [palette.index(pixel) for pixel in run] + [0]
                data.extend(indices[k] << 4 | indices[k + 1] for k in range(0, len(run), 2))
            elif kind == 2:
                for pixel in run:
                    data.extend(colour(pixel))
        return bytes(data)

    data = bytearray(b'STI2' + struct.pack('<HHBB2x', w, h, int(has_alpha), len(palette)))
    for pixel in palette:
        data.extend(colour(pixel))
    # Offsets count from the end of the table; a row equal to the one above
    # shares its data.
    offsets, body, last = [], bytearray(), None
    for row in rows:
        encoded = encode_row(row)
        if encoded != last:
            offsets.append(len(body))
            body.extend(encoded)
            last = encoded
        else:
            offsets.append(offsets[-1])
    data.extend(struct.pack('<%dI' % h, *offsets))
    data.extend(body)
    return bytes(data)


def encode_image(image, sti2=False):
    """STI1 unless `sti2`; then the smallest of STI1, STI2 direct and STI2
    indexed, in that order on a tie."""
    forms = [encode_sti1(image)]
    if sti2:
        forms += [encode_sti2(image), encode_sti2(image, indexed=True)]
    return min((form for form in forms if form is not None), key=len)


def source_file(root, name):
    if not isinstance(name, str) or len(name) > 116 or not re.fullmatch(r'[A-Za-z0-9_./-]+', name):
        raise ValueError('Invalid asset path')
    if any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Asset paths must be relative, without traversal')
    path = (root / PurePosixPath(name)).resolve()
    if not path.is_relative_to(root):
        raise ValueError('Asset symlink escapes theme directory')
    return path


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def build(directory):
    root = Path(directory).resolve()
    manifest_path = root / 'theme.json'
    if manifest_path.stat().st_size > 16384:
        raise ValueError('Manifest exceeds 16 KiB')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'), object_pairs_hook=unique_keys)
    validate_manifest(manifest_path)
    if type(manifest.get('spec')) is not int or manifest['spec'] not in (1, 2):
        raise ValueError('Expected spec: 1 or 2')
    # STI2 images only for spec 2 (#31): firmware up to 3.1.0 reads only
    # STI1, and refuses spec 2 at the manifest.
    sti2 = manifest['spec'] == 2
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,48}', manifest.get('theme', {}).get('id', '')):
        raise ValueError('Invalid theme ID')
    display = manifest.get('display', {})
    if display.get('width') != 240 or display.get('height') != 240:
        raise ValueError('Canvas must be 240x240')
    layers = manifest.get('layers')
    if not isinstance(layers, list) or len(layers) > 32:
        raise ValueError('Expected at most 32 layers')
    json_bytes = json.dumps(manifest, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
    if len(json_bytes) > 16384:
        raise ValueError('Manifest exceeds 16 KiB')
    entries = {'theme.json': json_bytes}
    for index, layer in enumerate(layers):
        kind = layer.get('type')
        if kind not in ('image', 'animation', 'text', 'shape'):
            raise ValueError(f'Unknown layer type: {kind}')
        if kind not in ('image', 'animation'):
            continue
        source = layer.get('source')
        source_file(root, source)  # validate the logical path before adding suffixes
        if kind == 'image':
            paths = [source]
        else:
            frames, fps = layer.get('frames'), layer.get('fps')
            if type(frames) is not int or not 1 <= frames <= 240 or type(fps) is not int or not 1 <= fps <= 15:
                raise ValueError('Animations require 1..240 frames and 1..15 FPS')
            paths = [f'{source}/{i:03d}.png' for i in range(frames)]
        for name in paths:
            asset_name = name + '.sti'
            if len(asset_name) > 120:
                raise ValueError('Compiled asset path exceeds 120 bytes')
            if asset_name in entries:
                continue
            try:
                with Image.open(source_file(root, name)) as image:
                    if kind == 'animation' and image.size != (layer.get('width'), layer.get('height')):
                        raise ValueError('frame dimensions must match width and height')
                    entries[asset_name] = encode_image(image, sti2)
            except (OSError, ValueError, Image.DecompressionBombError) as error:
                raise ValueError(f'layers[{index}].source: {name}: {error}') from error
            if len(entries) > 256 or sum(map(len, entries.values())) > MAX_PACKAGE:
                raise ValueError('Package exceeds 256 entries or 3 MiB')
    data = bytearray(b'STH1' + struct.pack('<HH', len(entries), 0))
    for name, payload in entries.items():
        path = name.encode('ascii')
        data.extend(struct.pack('<HI', len(path), len(payload)))
        data.extend(path)
        data.extend(payload)
    if len(data) > MAX_PACKAGE:
        raise ValueError('Package exceeds 3 MiB')
    # Validate the exact bytes to be installed, including all compiled frame dimensions.
    with tempfile.TemporaryDirectory() as scratch:
        package = Path(scratch)/'theme.stheme'
        package.write_bytes(data)
        validate_package(package)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        data = build(args.source)
        args.output.write_bytes(data)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f'Theme build failed: {error}\n')
    print(f'{args.output}: {len(data)} bytes')


if __name__ == '__main__':
    main()
