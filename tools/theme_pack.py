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


def encode_image(image):
    w, h = image.size
    if not (1 <= w <= 240 and 1 <= h <= 240):
        raise ValueError('Image dimensions must be 1..240 pixels')
    rgba = image.convert('RGBA')
    has_alpha = rgba.getextrema()[3][0] != 255
    data = bytearray(b'STI1' + struct.pack('<HHB3x', w, h, int(has_alpha)))
    for r, g, b, a in rgba.getdata():
        data.extend(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
        if has_alpha:
            data.append(a)
    return bytes(data)


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
    if type(manifest.get('spec')) is not int or manifest['spec'] != 1:
        raise ValueError('Expected spec: 1')
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
                    entries[asset_name] = encode_image(image)
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
