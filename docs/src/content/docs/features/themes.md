---
title: Theme clocks
description: Create, install, and run declarative animated clock faces on SmallTV Pro.
---

Theme Engine V1 is enabled on **SmallTV Pro (8 MB ESP32)**. A theme is data and
assets; it never contains executable code. Once the firmware supports themes,
creating another clock face requires only theme files.

## Install and select

1. Open **Display → Theme clocks** in the device web UI.
2. Choose a `.stheme` file and click **Install .stheme**.
3. Choose the installed theme and click **Use theme**. Selection survives reboot.
4. Set the timezone under **Clock & night mode**. Theme mode starts NTP even when
   night mode is off. Unsynchronized time fields display `--`.

The repository includes `examples/themes/pixel-room.stheme`, a complete example
with a background, animated cat, time, and date. Its source assets and manifest
are in `examples/themes/pixel-room/`.

Visual authoring is provided by the separate **SmallTV Studio** project. This
firmware repository keeps the format, validator, packer, and offline preview
tools so themes remain reproducible in CI without the editor application.

Notifications keep their existing behavior and cover the active theme. When the
notification ends, the theme is restored with the current time and animation
frame. Theme mode is selected directly; it is not part of the mode carousel in V1.

Use **Remove** to delete an inactive theme. To replace the currently selected
package, first choose another display mode and **Save settings**, remove the old
package, then install the replacement. Duplicate installed IDs are rejected.
Unreadable or invalid packages remain listed with their filename ID, size, and
validation error. **Use theme** is disabled for them, but **Remove** remains
available, including when a corrupt package was selected. Removing that package
clears the saved selection; install or select a valid theme to resume the clock.
Up to 16 packages can coexist, subject to available filesystem space. Firmware
updates do not require reinstalling packages. Settings export contains the selected
ID, not the package files; keep your source themes and `.stheme` files separately.

## Create a source theme

```text
pixel-room/
├── theme.json
├── images/background.png
├── animations/cat/000.png
├── animations/cat/001.png
└── fonts/                  (reserved for future compiled fonts)
```

The desktop tools require Python 3.10+, Pillow and a C++11 compiler (`c++`, `g++`,
or `clang++`; set `CXX` if needed). On Linux/macOS/WSL, prepare the dependencies:

```bash
python3 -m venv /tmp/smalltv-theme-venv
source /tmp/smalltv-theme-venv/bin/activate
pip install -r tools/requirements-theme.txt
pio pkg install -e smalltv_esp32_8mb
```

PlatformIO provides the ArduinoJson headers and built-in font used by the firmware.
An existing firmware build already has them. No device or firmware recompilation
is needed: the native desktop adapter compiles automatically on first use and is
cached under `.pio/theme-host/`. Alternatively, point `SMALLTV_ARDUINOJSON_INCLUDE`
and `SMALLTV_GFX_INCLUDE` at library source directories containing `ArduinoJson.h`
and `font/glcdfont.h`.

Validate, build and preview a theme:

```bash
python3 tools/smalltv_theme.py validate examples/themes/pixel-room
python3 tools/smalltv_theme.py build examples/themes/pixel-room /tmp/pixel-room.stheme
python3 tools/smalltv_theme.py validate /tmp/pixel-room.stheme
python3 tools/smalltv_theme.py preview /tmp/pixel-room.stheme /tmp/pixel-room.html \
  --time 2026-09-18T10:24:55 --seconds 10 --fps 15
```

`validate` accepts a source directory or an installed-format `.stheme`. Both paths
use the **firmware's actual C++ validation**, including every compiled frame. Source
validation also checks asset decoding and safe relative paths. Errors identify the
field or asset, for example `layers[0].color: expected a color in #RRGGBB form`.
`build` validates before writing its output. The original
`python3 tools/theme_pack.py SOURCE OUTPUT.stheme` command remains supported and
now performs the same complete validation.

Open the generated HTML in a browser. It contains the images and controls needed
for offline playback, pause, seeking and replay. A ready-made example lives at
`examples/themes/pixel-room-preview.html`; simply open it, without installing tools.
Preview generation accepts source directories too. It uses the same C++ compositor,
RGB565 blending and bitmap font as the device, and advances time across day/month/year
boundaries. `--time` is the desired local wall time without a UTC offset, defaulting
to the computer's current wall time. Duration is 1–60 seconds, sampled at 1–15 FPS.
The recorded clip stops at its end; Replay restarts it. A lower preview sampling
rate can skip intermediate animation frames. Panel correction, hardware latency
and notifications are not simulated. The preview's playback controls are desktop
HTML, never included in `.stheme` packages.

This utility converts image pixels on your computer. The device reads RGB565
assets directly; it does not decode PNG/GIF/TTF. The package stores the same
manifest values, including logical source paths. A future compiler can replace
the asset provider without changing those paths. GIF extraction, custom fonts,
compression, and advanced compiler features are not implemented in V1.

## Manifest

```json
{
  "spec": 1,
  "theme": {"id": "minimal-clock", "name": "Minimal Clock", "author": "You", "version": "1.0.0"},
  "display": {"width": 240, "height": 240, "background": "#000000"},
  "layers": [
    {"id": "clock", "type": "text", "x": 120, "y": 100, "anchor": "center", "value": "{HH}:{MM}", "size": 48, "color": "#ffffff"},
    {"id": "date", "type": "text", "x": 120, "y": 150, "anchor": "center", "value": "{WD} {DD} {MON}", "size": 16, "color": "#aaaaaa"}
  ]
}
```

All fields shown at the root, in `theme`, and in `display` are required. The
canvas is always 240×240 with the origin at the top left. Coordinates and sizes
are integer pixels. Layers draw in array order, back to front; IDs must be unique.
Unknown fields, unsupported types, invalid values, and unknown text variables are
rejected. Nothing evaluates expressions or runs scripts.

## Small JSON data sources

A theme may declare up to four small JSON sources. The firmware fetches each URL
periodically and exposes only the declared fields to text layers. A response is
limited to 2 KiB, and a source can declare at most eight fields. The minimum
refresh interval is 10 seconds.

Fetching runs in a background task on the Pro, with one request in flight at a
time. Connect and TLS handshake timeouts are three seconds; header/body reads
also check a three-second overall request deadline. A stalled or slowly trickling
response cannot hold up the display loop. The worker's 8 KiB stack exists only
while fetching. Switching themes cancels the old request and discards its result.
Failed requests keep the last displayed values until the next scheduled attempt.
When a value changes, only text whose rendered content changed is repainted,
including the area needed to erase its previous contents.

```json
{
  "data": [
    {
      "id": "weather",
      "url": "https://example.local/weather.json",
      "interval": 300,
      "fields": [
        {"id": "temp", "path": "main.temp"},
        {"id": "city", "path": "name"}
      ]
    }
  ],
  "layers": [
    {"id": "reading", "type": "text", "x": 120, "y": 90,
     "anchor": "center", "value": "{weather.city} {weather.temp}C",
     "size": 16, "color": "#ffffff"}
  ]
}
```

The `path` is a dotted path through JSON objects. Values are rendered as short
strings; missing values show `--`. URLs must use `http://` or `https://`. Data
fetching is owned by theme mode and does not affect notifications or other
display modes. HTTPS uses the firmware's existing insecure TLS client, matching
the other device-side JSON integrations.

Every layer needs `id`, `type`, `x`, and `y`. The device clips geometry outside the
canvas. Colors are exactly `#RRGGBB`, case insensitive; asset alpha is supported.

### Text

Required fields: `value`, `size`, `color`. Optional `anchor` defaults to `top-left`.
Anchors position the complete text cell rectangle:

| Vertical position | Left | Center | Right |
| --- | --- | --- | --- |
| Top | `top-left` | `top-center` | `top-right` |
| Center | `center-left` | `center` | `center-right` |
| Bottom | `bottom-left` | `bottom-center` | `bottom-right` |

V1 uses the built-in ASCII bitmap font. `size` is the exact cell height in pixels,
8–96; cell width is `ceil(size × 6 / 8)`. No wrapping or multiline text. Sizes
8, 16, 24, etc. give uniform pixel scaling. Text is transparent over lower layers.
Month and weekday names are English; the device's configured timezone applies.

| Token | Value |
| --- | --- |
| `{HH}` | 00–23 hour |
| `{hh}` | 01–12 hour |
| `{MM}` | 00–59 minute |
| `{SS}` | 00–59 second |
| `{DD}` | 01–31 day |
| `{MON}` / `{MONTH}` | Abbreviated / full month |
| `{WD}` / `{WEEKDAY}` | Abbreviated / full weekday |
| `{YYYY}` | Year |

### Image

```json
{"id":"background","type":"image","x":0,"y":0,"source":"images/background.png"}
```

Dimensions come from the asset, between 1×1 and 240×240. No runtime scaling.
PNG alpha is preserved and composited against the layers below it.

### Shape

```json
{"id":"box","type":"shape","shape":"rectangle","x":20,"y":20,"width":200,"height":60,"fill":"#102030","stroke":"#ffffff","strokeWidth":2}
```

```json
{"id":"ring","type":"shape","shape":"circle","x":120,"y":120,"radius":110,"stroke":"#ffffff","strokeWidth":2}
```

```json
{"id":"line","type":"shape","shape":"line","x":20,"y":180,"x2":220,"y2":180,"stroke":"#ffffff","strokeWidth":2}
```

Rectangles use a top-left origin; circles use a center and radius. Rectangle and
circle strokes are inside their outer bounds. Lines use two endpoints, with a
centered stroke and round ends. `strokeWidth` defaults to 1 and supports 1–32.
Rectangles and circles need `fill`, `stroke`, or both; lines need `stroke`.

### Animation

```json
{"id":"cat","type":"animation","x":160,"y":170,"width":64,"height":64,"source":"animations/cat","frames":12,"fps":8,"loop":true}
```

All shown fields are required. Source frames are `000.png`, `001.png`, etc.; their
dimensions must exactly match `width` and `height`. Each animation has its own
elapsed-time schedule, independent of clock text. Late rendering skips to the due
frame instead of queuing work. `loop:false` holds the final frame. Pausing under a
notification catches up when the theme becomes visible. Switching themes restarts
the animation. Only the required pixel rows are read, never a whole animation.
The renderer resolves each visible image/frame once per render pass and reuses
its asset handle across rows and dirty regions.

## Limits and storage

| Item | V1 limit |
| --- | --- |
| Manifest | 16 KiB, nesting depth 8 |
| Layers | 32 |
| IDs | 1–48 ASCII letters, digits, `_`, `-` |
| Theme name / author / version | 96 / 96 / 32 bytes |
| Text template | 1–128 printable ASCII bytes |
| Coordinates | −240 through 479, clipped to the canvas |
| Rectangle/image/frame dimensions | 1–240 pixels |
| Circle radius | 1–240 pixels |
| Animation | 1–240 frames, 1–15 FPS |
| Container | 256 entries, at most 3 MiB |
| Installed packages | 16, within available storage |

Packages live under `/themes/<id>.stheme`. Upload uses `/themes/.upload`; every
entry, manifest, and referenced animation frame is checked before a rename makes
the package visible. Failed or interrupted uploads leave existing themes and
selection intact. Boot removes an abandoned staging file. Installation reserves
space for settings and filesystem metadata. Settings saves use a temporary file,
check the complete byte count, then rename over the old config.

The engine holds a bounded entry index and layer state in RAM. Compositing uses
row buffers, not a 115,200-byte framebuffer. Static scenes produce no display
writes after the first draw. Clock text is compared when wall time changes;
animations invalidate only when their visible frame changes. Dirty areas are
recomposited through all intersecting layers, restoring old glyphs, transparent
sprite pixels, and foreground layers correctly.

10–15 FPS and clock updates under 100 ms remain hardware performance targets.
The automated tests exercise composition and scheduling; they do not measure
LittleFS/SPI latency on an actual SmallTV Pro.

## Device package format

`.stheme` V1 is a small uncompressed binary container, **not a ZIP file**. All
integers are unsigned little endian. No extracted paths become filesystem paths.

| Container field | Encoding |
| --- | --- |
| Magic | Four ASCII bytes `STH1` |
| Entry count | `uint16`, 1–256 |
| Reserved | `uint16`, zero |
| Each entry | `uint16` path byte count, `uint32` payload byte count, path bytes, payload |

Paths are relative ASCII letters/digits plus `/`, `.`, `_`, `-`, at most 120 bytes.
Empty components, `.` and `..`, absolute paths, backslashes, and duplicates are
rejected. There must be one `theme.json`. Other entries must be `.sti` images.
No padding, trailing bytes, executable entries, or external URLs are accepted.

The asset resolver appends `.sti` to an image's logical `source`, or resolves
`<animation source>/<zero-padded three-digit frame>.png.sti`. For example,
`images/background.png` maps to `images/background.png.sti` inside the package.

| STI field | Encoding |
| --- | --- |
| Magic | Four ASCII bytes `STI1` |
| Width, height | Two `uint16`, each 1–240 |
| Alpha flag | `uint8`: 0 for opaque, 1 for alpha |
| Reserved | Three zero bytes |
| Pixels | Row-major RGB565 `uint16`, followed by alpha `uint8` if flag=1 |

Alpha 0 is transparent, 255 opaque. Intermediate values blend RGB565 channels
with rounding. Opaque files use two bytes/pixel, transparent files three. The
payload length must match the dimensions exactly.

## HTTP API

All routes use the device's existing optional digest authentication, including
upload callbacks. Responses are JSON; failures contain `error`.

| Method and route | Request / result |
| --- | --- |
| `GET /api/themes` | All installed packages with `valid`, `error`, filename-derived `id`, metadata, bytes; selected ID, free bytes, renderer error |
| `POST /api/themes/install` | One multipart file; returns `201` and `{ "id": "..." }` |
| `POST /api/themes/select` | JSON `{ "id": "..." }`; validates, saves, activates, starts NTP |
| `POST /api/themes/delete` | JSON `{ "id": "..." }`; rejects deletion of a valid active theme; permits removal of a corrupt selected package |

Invalid uploads return 400; duplicate IDs and valid active-theme deletion return 409; missing
themes return 404. The normal settings JSON also exposes `mode: "theme"` and
`themeId`. Prefer the selection endpoint because it validates the installed file
before saving. An unavailable selected package displays an error while the web UI
remains available for recovery.

## Development checks

After resolving PlatformIO's Pro dependencies:

```bash
sh tests/theme/run.sh
python3 -m unittest discover -s tests/theme -p 'test_*.py'
node tests/theme/test_webui.js
pio run -e smalltv_esp32_8mb -e smalltv_lean
```

The native suite uses the same C++ parser, scheduler, package reader, and compositor
as the firmware. It also loads the example package, renders its frames over five
minutes of simulated time, and writes `/tmp/smalltv-theme-preview.ppm`.

An optional real-browser check exercises offline playback, pause, seek and replay
in the checked-in example. It needs Node 22+ and Chrome/Chromium:

```bash
node tests/theme/test_preview_browser.mjs
# Set CHROME=/path/to/chromium when the browser is not named google-chrome.
```
