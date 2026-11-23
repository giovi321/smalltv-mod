#!/usr/bin/env python3
"""Simulate the SmallTV rain radar screen, as if the device were plugged in.

    python3 tools/rain_sim/simulate.py --lat 48.39 --lon -4.49 --out /tmp/rain-sim
    python3 tools/rain_sim/simulate.py ... --outage      # then RainViewer stops answering
    python3 tools/rain_sim/simulate.py ... --preview docs/public/assets/rain-preview.png

1. Downloads RainViewer's current frame list and the 13 tiles the device
   would request (same URL, position rounded to 2 decimals like the firmware).
2. Builds tools/rain_sim/sim.cpp with the firmware's own rain code and the
   real GFX Library for Arduino (from a PlatformIO build's libdeps).
3. Runs the device timeline in virtual time: boot screens, the CONNECTED
   screen, the first fetch (frames appear one by one, ~20 s per tile as on
   the device), then the animation loop.
4. Writes every distinct screen as a PNG (2x), screens.gif (the timeline,
   frame delays in device time, sped up by --speed) and contact.png.

Not simulated: the panel's colour gains (Display tab, default 100 %), the
blit's top-to-bottom sweep (frames appear at once here), the web UI.
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
UA = {"User-Agent": "Mozilla/5.0 (smalltv-mod simulator)"}


def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30).read()


def get_tiles(lat, lon, tiles):
    os.makedirs(tiles, exist_ok=True)
    wm = fetch("https://api.rainviewer.com/public/weather-maps.json")
    open(os.path.join(tiles, "weather-maps.json"), "wb").write(wm)
    d = json.loads(wm)
    host = d["host"]
    lines = []
    for f in d["radar"]["past"][-13:] + d["radar"].get("nowcast", []):
        name = "%d.png" % f["time"]
        url = "%s%s/256/7/%.2f/%.2f/2/1_1.png" % (host, f["path"], lat, lon)
        open(os.path.join(tiles, name), "wb").write(fetch(url))
        lines.append("%s %s" % (f["path"], name))
    open(os.path.join(tiles, "tiles.txt"), "w").write("\n".join(lines) + "\n")
    print("tiles: %d frames around %.2f, %.2f" % (len(lines), lat, lon))


def find_lib(name):
    hits = sorted(glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", name)))
    if not hits:
        sys.exit("%s not found under .pio/libdeps: build a firmware env first (pio run -e smalltv_lean)" % name)
    return hits[0]


def build(out):
    gfx = os.path.join(find_lib("GFX Library for Arduino"), "src")
    json_inc = os.path.join(find_lib("ArduinoJson"), "src")
    exe = os.path.join(out, "sim")
    src = [os.path.join(HERE, "sim.cpp"), os.path.join(HERE, "sim_gfx.cpp"),
           os.path.join(ROOT, "src/features/rain/RainMode.cpp"),
           os.path.join(ROOT, "src/features/rain/RainClient.cpp"),
           os.path.join(ROOT, "src/features/rain/RainPng.cpp"),
           os.path.join(ROOT, "src/Settings.cpp"),
           os.path.join(gfx, "Arduino_GFX.cpp"), os.path.join(gfx, "Arduino_G.cpp"),
           os.path.join(gfx, "canvas/Arduino_Canvas.cpp")]
    cmd = ["g++", "-std=c++17", "-O2", "-w",
           "-DSMALLTV_ESP8266", "-DARDUINOJSON_USE_DOUBLE=0", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1",
           "-include", os.path.join(ROOT, "tests/rain/shim/rain_host.h"),
           "-include", os.path.join(ROOT, "tests/fuzz/shim/progmem.h"),
           "-I" + os.path.join(HERE, "shim"), "-I" + os.path.join(ROOT, "tests/rain/shim"),
           "-I" + os.path.join(ROOT, "tests/fuzz/shim"), "-I" + os.path.join(ROOT, "src"),
           "-I" + os.path.join(ROOT, "src/features/rain"), "-I" + gfx, "-I" + json_inc,
           *src, "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def to_image(path):
    from PIL import Image
    raw = open(path, "rb").read()
    px = bytearray()
    for i in range(0, len(raw), 2):
        v = raw[i] | raw[i + 1] << 8
        r, g, b = v >> 11, (v >> 5) & 63, v & 31
        px += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return Image.frombytes("RGB", (240, 240), bytes(px))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--range", type=int, default=50, help="range in km (10-100), default 50")
    ap.add_argument("--anim", type=int, default=3, help="seconds per frame in the loop, default 3")
    ap.add_argument("--ui", type=int, default=1, help="text & city size 0/1/2, default 1 (medium)")
    ap.add_argument("--out", required=True, help="output directory (emptied first)")
    ap.add_argument("--tiles", help="tile directory: reused if it holds tiles, else filled (default OUT-tiles)")
    ap.add_argument("--outage", action="store_true", help="then simulate RainViewer going away")
    ap.add_argument("--wind", help="turn the wind arrow on with this wind: FROM,KMH (direction it comes from)")
    ap.add_argument("--wind-unit", type=int, default=0, help="wind speed unit: 0 as the distances, 1 km/h, 2 mph, 3 knots, 4 m/s")
    ap.add_argument("--speed", type=float, default=8.0, help="GIF speed-up of device time, default 8")
    ap.add_argument("--preview", help="also save the most colourful radar screen (2x) to this PNG")
    a = ap.parse_args()

    from PIL import Image   # noqa: F401  (fail early if Pillow is missing)
    tiles = a.tiles or os.path.join(a.out + "-tiles")
    if not a.tiles or not os.path.exists(os.path.join(tiles, "tiles.txt")):
        get_tiles(a.lat, a.lon, tiles)
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    exe = build(a.out)
    screens = os.path.join(a.out, "screens")
    os.makedirs(screens)
    subprocess.run([exe, tiles, screens, str(a.lat), str(a.lon), str(a.range), str(a.anim), str(a.ui)]
                   + (["outage"] if a.outage else []) + (["wind=" + a.wind, "windunit=%d" % a.wind_unit] if a.wind else []), check=True)

    rows = [l.split(",", 2) for l in open(os.path.join(screens, "timeline.csv")).read().splitlines()]
    imgs = []
    for idx, ms, label in rows:
        im = to_image(os.path.join(screens, "%04d.rgb565" % int(idx))).resize((480, 480), 0)
        im.save(os.path.join(screens, "%04d.png" % int(idx)))
        imgs.append((int(ms), label, im))
    durations = [max(20, int((imgs[i + 1][0] - imgs[i][0]) / a.speed)) for i in range(len(imgs) - 1)] + [2000]
    imgs[0][2].save(os.path.join(a.out, "screens.gif"), save_all=True, append_images=[i[2] for i in imgs[1:]],
                    duration=durations, loop=0)
    # Contact sheet: each distinct screen once (the loop repeats them), at 1x,
    # 8 per row, with the device time it first appeared.
    from PIL import Image, ImageDraw
    seen, unique = set(), []
    for ms, label, im in imgs:
        key = im.tobytes()
        if key not in seen:
            seen.add(key)
            unique.append((ms, label, im))
    imgs = unique
    cols = 8
    sheet = Image.new("RGB", (cols * 250, ((len(imgs) + cols - 1) // cols) * 262), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for n, (ms, label, im) in enumerate(imgs):
        x, y = (n % cols) * 250 + 5, (n // cols) * 262 + 5
        sheet.paste(im.resize((240, 240), 0), (x, y))
        dr.text((x, y + 242), "%6.1f s  %s" % (ms / 1000.0, label), fill=(220, 220, 220))
    sheet.save(os.path.join(a.out, "contact.png"))
    if a.preview:
        # The radar screen showing the most colours of the scheme.
        radar = [i[2] for i in imgs if i[1].startswith("rain (ok")] or [i[2] for i in imgs]
        best = max(radar, key=lambda im: len(im.getcolors(1 << 16) or []))
        best.save(a.preview, optimize=True)
        print("preview:", a.preview)
    print("screens: %d -> %s/screens/*.png, screens.gif, contact.png" % (len(imgs), a.out))


if __name__ == "__main__":
    main()
