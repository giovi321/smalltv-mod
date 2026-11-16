#!/usr/bin/env python3
"""Convert a Material Design Icon to the hex `d` field of a SmallTV bitmap primitive.

Usage:
    python tools/mdi_to_hex.py window-open
    python tools/mdi_to_hex.py thermometer --size 48 --color "#FFCC00" --preview
    python tools/mdi_to_hex.py path/to/local-icon.svg --size 32
    python tools/mdi_to_hex.py "https://example.com/path/to/remote-icon.svg" --size 24

Given an icon name, the SVG is fetched from the MaterialDesign-SVG repo
(https://raw.githubusercontent.com/Templarian/MaterialDesign-SVG/master/svg/<name>.svg).
MDI SVGs are a single path on a 24x24 viewBox; the path is scaled to the
requested size, rasterised to a 1-bit bitmap, and packed row-major,
MSB-first, exactly what the firmware's bitmap primitive expects in `d`.
"""

import argparse
import math
import os
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

try:
    from svg.path import parse_path
    from svg.path.path import Move
    from PIL import Image, ImageChops, ImageDraw
except ImportError:
    sys.exit(
        "error: missing dependencies (svg.path, Pillow). Install them into an\n"
        "isolated env, for example:\n"
        "    python -m venv tools/.venv-mdi\n"
        "    tools/.venv-mdi/Scripts/python -m pip install -r tools/requirements-mdi.txt\n"
        "then run this script with that interpreter."
    )

MDI_URL = "https://raw.githubusercontent.com/Templarian/MaterialDesign-SVG/master/svg/{}.svg"
SS = 16  # supersampling factor for rasterisation


def load_svg(source: str) -> str:
    """Return the SVG text from an MDI icon name, local file or a remote HTTP(S) URL."""

    if os.path.isfile(source):
        with open(source, "r", encoding="utf-8") as fh:
            return fh.read()

    if source.startswith(("https://", "http://")):
        try:
            req = urllib.request.Request(
                source,
                headers={"User-Agent": "SmallTV-MDI-Converter/1.0"}
            )
            with urllib.request.urlopen(req, timeout=20) as resp:
                data = resp.read(2 * 1024 * 1024 + 1)

                if len(data) > 2 * 1024 * 1024:
                    sys.exit("error: SVG exceeds 2 MB limit")

                text = data.decode("utf-8")

                try:
                    root = ET.fromstring(text)
                except ET.ParseError:
                    sys.exit("error: URL did not return valid SVG content")

                if not root.tag.endswith("svg"):
                    sys.exit("error: URL did not return an SVG document")

                return text

        except urllib.error.HTTPError as exc:
            sys.exit(f"error: fetching SVG failed: HTTP {exc.code}")
        except urllib.error.URLError as exc:
            sys.exit(f"error: fetching SVG failed: {exc.reason}")
        except UnicodeDecodeError:
            sys.exit("error: SVG is not valid UTF-8")

    if source.lower().endswith(".svg"):
        sys.exit(f"error: file not found: {source}")

    if not all(ch.isalnum() or ch == "-" for ch in source):
        sys.exit(f"error: not a valid MDI icon name, file, or URL: {source!r}")

    url = MDI_URL.format(source)

    try:
        with urllib.request.urlopen(url, timeout=20) as resp:
            return resp.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            sys.exit(f"error: no MDI icon named {source!r} (404 from {url})")
        sys.exit(f"error: fetching {url} failed: HTTP {exc.code}")
    except urllib.error.URLError as exc:
        sys.exit(f"error: fetching {url} failed: {exc.reason}")


def extract_path(svg_text: str) -> tuple[str, float]:
    """Return (path d attribute, viewBox size) from an MDI-style SVG."""
    try:
        root = ET.fromstring(svg_text)
    except ET.ParseError as exc:
        sys.exit(f"error: not a valid SVG: {exc}")
    view_box = root.get("viewBox", "0 0 24 24").split()
    box = float(view_box[3]) if len(view_box) == 4 else 24.0
    for el in root.iter():
        if el.tag.endswith("path") and el.get("d"):
            return el.get("d"), box
    sys.exit("error: no <path d=...> found in the SVG")


def subpath_polygons(d: str, scale: float):
    """Approximate each subpath of the SVG path with a dense polygon."""
    polys, current = [], None
    for seg in parse_path(d):
        if isinstance(seg, Move):
            if current:
                polys.append(current)
            current = []
            continue
        if current is None:
            current = []
        n = max(8, math.ceil(seg.length() * scale))
        for i in range(n + 1):
            pt = seg.point(i / n)
            current.append((pt.real * scale, pt.imag * scale))
    if current:
        polys.append(current)
    return [p for p in polys if len(p) >= 3]


def rasterize(d: str, size: int, box: float) -> list[list[bool]]:
    """Render the path to a size x size boolean grid (even-odd fill)."""
    scale = size * SS / box
    base = Image.new("L", (size * SS, size * SS), 0)
    for poly in subpath_polygons(d, scale):
        layer = Image.new("L", base.size, 0)
        ImageDraw.Draw(layer).polygon(poly, fill=255)
        base = ImageChops.difference(base, layer)  # binary difference == XOR
    img = base.resize((size, size), Image.LANCZOS)
    px = img.load()
    return [[px[x, y] >= 128 for x in range(size)] for y in range(size)]


def pack_hex(bits: list[list[bool]]) -> str:
    size = len(bits)
    out = bytearray(math.ceil(size * size / 8))
    for y in range(size):
        for x in range(size):
            if bits[y][x]:
                idx = y * size + x
                out[idx // 8] |= 0x80 >> (idx % 8)
    return out.hex()


def ascii_preview(bits: list[list[bool]]) -> str:
    return "\n".join(
        "".join("#" if bit else "." for bit in row) for row in bits
    )


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Convert an MDI icon, local SVG or remote SVG URL to a SmallTV bitmap hex string."
    )
    ap.add_argument("icon", help="MDI icon name, local SVG file, or HTTP(S) SVG URL")
    ap.add_argument("--size", type=int, choices=(16, 24, 32, 48), default=24,
                    help="bitmap width/height in px (default 24; 48 only fits ESP32 payloads)")
    ap.add_argument("--color", default="#FFFFFF", help="colour for the JSON fragment (default #FFFFFF)")
    ap.add_argument("--preview", action="store_true", help="print an ASCII-art preview of the bitmap")
    args = ap.parse_args()

    d_attr, box = extract_path(load_svg(args.icon))
    bits = rasterize(d_attr, args.size, box)
    hexstr = pack_hex(bits)
    assert len(hexstr) == math.ceil(args.size * args.size / 8) * 2

    fragment = (
        f'{{"t":"bitmap","x":120,"y":30,"w":{args.size},"h":{args.size},'
        f'"c":"{args.color}","a":"c","d":"{hexstr}"}}'
    )
    print("JSON fragment for a screen payload:")
    print(fragment)
    print()
    print(f"Raw hex ({len(hexstr)} chars, for the blueprint's bitmap field):")
    print(hexstr)
    if args.preview:
        print()
        print(ascii_preview(bits))


if __name__ == "__main__":
    main()
