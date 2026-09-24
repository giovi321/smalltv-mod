#!/usr/bin/env python3
"""Reproduce the example's original geometric pixel assets. Requires Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).parent / 'pixel-room'
background = Image.new('RGB', (240, 240), '#152238')
draw = ImageDraw.Draw(background)
draw.rectangle((0, 200, 239, 239), fill='#28364a')
for x in range(0, 240, 30):
    draw.line((x, 201, x - 20, 239), fill='#384458')
draw.rectangle((25, 115, 99, 184), fill='#354961', outline='#8198ad', width=3)
draw.rectangle((31, 121, 93, 178), fill='#10192f')
draw.rectangle((58, 118, 62, 181), fill='#8198ad')
draw.rectangle((28, 146, 97, 150), fill='#8198ad')
for x, y in [(39, 130), (76, 139), (83, 161), (46, 168)]:
    draw.rectangle((x, y, x + 1, y + 1), fill='#ffdf91')
background.save(root / 'images/background.png')
for frame in range(4):
    image = Image.new('RGBA', (48, 48))
    d = ImageDraw.Draw(image)
    d.rectangle((9, 24, 35, 44), fill='#df9870')
    d.rectangle((7, 12, 34, 32), fill='#f0b184')
    d.polygon([(7, 14), (7, 3), (18, 14)], fill='#f0b184')
    d.polygon([(24, 14), (34, 3), (34, 14)], fill='#f0b184')
    d.rectangle((13, 20, 15, 20 if frame == 2 else 23), fill='#263348')
    d.rectangle((26, 20, 28, 20 if frame == 2 else 23), fill='#263348')
    d.rectangle((20, 27, 22, 28), fill='#a45b63')
    height = [27, 23, 19, 23][frame]
    d.line([(34, 39), (42, 39), (42, height)], fill='#df9870', width=5)
    image.save(root / f'animations/cat/{frame:03d}.png')
