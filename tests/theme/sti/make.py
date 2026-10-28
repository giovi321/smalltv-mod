"""Writes the shared STI test files in this folder from two small source
images, each in every form the packer can write (see README.md).
Usage: python3 tests/theme/sti/make.py
"""
import importlib.util
import json
from pathlib import Path
import sys

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('theme_pack', ROOT/'tools/theme_pack.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

WIDTH = 70   # more than 64, so long runs are split


def alpha_image():
    """6 colours with alpha: transparent rows (shared), half-transparent
    pixels, repeats, odd literal runs and transparent runs over 64."""
    t, red, blue_half = (0, 0, 0, 0), (255, 0, 0, 255), (0, 0, 255, 128)
    white, green, red_half, yellow = (255, 255, 255, 255), (0, 255, 0, 255), (255, 0, 0, 128), (255, 255, 0, 255)
    rows = [[t] * WIDTH,
            [t] * WIDTH,
            [red] * 3 + [blue_half] + [t] * (WIDTH - 4),
            [(white, green, red_half)[x % 3] for x in range(WIDTH)],
            [t] + [yellow] * 7 + [t] * (WIDTH - 8)]
    image = Image.new('RGBA', (WIDTH, len(rows)))
    image.putdata([pixel for row in rows for pixel in row])
    return image


def opaque_image():
    """15 colours, no alpha: a literal run over 64 (twice, shared), a repeat
    over 64, and runs of 1, 3 and 5."""
    colours = [(i * 18, 255 - i * 18, (i * 37) % 256) for i in range(14)]
    a, b, c, d = colours[1], colours[5], colours[9], colours[13]
    rows = [[colours[x % 14] for x in range(WIDTH)],
            [colours[x % 14] for x in range(WIDTH)],
            [(40, 80, 120)] * WIDTH,
            ([a] * 3 + [b] + [c] * 5 + [d]) * 7]
    image = Image.new('RGB', (WIDTH, len(rows)))
    image.putdata([pixel for row in rows for pixel in row])
    return image


def build():
    """Returns ({file name: bytes}, {file name: expected pixels}). Each pixel
    is [RGB565, alpha]; a fully transparent pixel is [0, 0]."""
    files, expected = {}, {}
    for source, image in (('alpha', alpha_image()), ('opaque', opaque_image())):
        rgba = image.convert('RGBA')
        pixels = [[pack.rgb565(r, g, b), a] if a else [0, 0] for r, g, b, a in rgba.getdata()]
        for form, data in (('sti1', pack.encode_sti1(image)), ('sti2-direct', pack.encode_sti2(image)),
                           ('sti2-indexed', pack.encode_sti2(image, indexed=True))):
            name = '%s-%s.sti' % (source, form)
            files[name] = data
            expected[name] = {'width': image.width, 'height': image.height, 'pixels': pixels}
    return files, expected


if __name__ == '__main__':
    files, expected = build()
    for name, data in files.items():
        (HERE/name).write_bytes(data)
    (HERE/'expected.json').write_text(json.dumps(expected, sort_keys=True) + '\n')
    print('wrote %d files' % len(files))
