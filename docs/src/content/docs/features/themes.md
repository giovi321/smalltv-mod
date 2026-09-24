---
title: Theme clocks
description: Create, install, and run declarative animated clock faces.
---

Theme Engine V1 is available on every display target except the lean ESP8266
image (`smalltv_lean`), which drops it to keep the heap headroom it exists to
protect. A theme is data and assets; it never contains executable code. Once
the firmware supports themes, creating another clock face requires only theme
files.

## Install and select

1. Open **Display → Theme clocks** in the device web UI.
2. Choose a `.stheme` file and click **Install .stheme**.
3. Choose the installed theme and click **Use theme**. Selection survives reboot.
4. Set the timezone under **Clock & night mode**. Theme mode starts NTP even when
   night mode is off. Unsynchronized time fields display `--`.

The repository includes `examples/themes/pixel-room.stheme`, a complete example
with a background, animated cat, time, and date. Its source assets and manifest
are in `examples/themes/pixel-room/`. `examples/themes/live-status.stheme`
demonstrates scrolling text and fetched-data bindings; see
[Example: a dynamic dashboard](#example-a-dynamic-dashboard).

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

A theme that declares `data` fields has nothing to fetch on a desktop, so preview
generation accepts repeatable `--data source.field=value` arguments and holds
those values for every sampled frame instead:

```bash
python3 tools/smalltv_theme.py preview examples/themes/live-status \
  examples/themes/live-status-preview.html --seconds 8 --fps 15 \
  --time 2026-09-22T12:00:00 \
  --data status.label='SmallTV dynamic dashboard headline' \
  --data status.level=82 --data status.state=Warning
```

Each key must reference a field declared in the theme's `data` block, exactly
once; an unknown key, a missing `=`, an empty key, a duplicate key, or more than
32 values fails the command before it writes any output. Injected values only
affect the preview frames; they cannot change package validation or which
assets are read. `examples/themes/live-status` is the reference fixture for this
workflow — see [Example: a dynamic dashboard](#example-a-dynamic-dashboard).

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
      "insecureTls": true,
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
strings; missing values show `--`. URLs must use `http://` or `https://`. Because
this path cannot validate server certificates, an HTTPS source must explicitly
set `"insecureTls": true`; omitting it rejects the package instead of silently
accepting unauthenticated TLS. Data fetching is owned by theme mode and does not
affect notifications or other display modes.

Installing a theme installs its data sources too: the device will periodically
poll every URL the manifest declares, including addresses on your LAN. Only
install themes from sources you trust — a manifest's `data` block is not
sandboxed beyond the request limits above (2 KiB response, one in-flight
request, three-second deadline).

Fetched values are scalars used as-is: to interpolate into text (above), or to
drive a layer's position, size, or color through the bindings below. There are
no history buffers, line charts, arrays, aggregation, arithmetic expressions, or
conditions in V1 — a fetched number maps to a property through exactly one
declared, bounded rule.

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

#### Scrolling text

A text layer whose rendered content is wider than a declared viewport can
scroll horizontally, instead of being clipped:

```json
{"id": "headline", "type": "text", "x": 20, "y": 40, "value": "{status.label}",
 "size": 12, "color": "#ffffff",
 "scroll": {"width": 200, "mode": "loop", "speed": 30, "pause": 1200, "gap": 24}}
```

| `scroll` field | Type | Range | Required | Meaning |
| --- | --- | --- | --- | --- |
| `width` | integer | 1–240 px | yes | Width of the clipping viewport |
| `mode` | string | `"mode": "loop"`, `"mode": "bounce"` | yes | Motion behavior |
| `speed` | integer | 1–240 px/s | yes | Horizontal speed |
| `pause` | integer | 0–10000 ms | no, default 1000 | Hold time at the initial position, and for bounce, at both ends |
| `gap` | integer | 0–240 px | loop only, default 24 | Blank space between repeated copies |

`gap` is rejected in bounce mode; it has no effect there. `x`, `y`, and `anchor`
position the viewport, not the complete unscrolled string — `x: 120` with
`anchor: center` centers the 200px viewport at x=120, not the full text. The
viewport height is `size`; its width is `scroll.width`. It clips to the 240×240
canvas like any other layer, and text is additionally clipped to the viewport
even when a wider dirty region from another layer overlaps it.

If the rendered text fits within the viewport, it stays fixed at its initial
position and never schedules a repaint. Otherwise scrolling starts after
`pause` milliseconds. In `loop` mode, the text moves continuously in one
direction; once it has scrolled past its own width plus `gap`, it wraps back to
the start and pauses again — the gap is where a second, repeated copy would
begin. In `bounce` mode, the text moves to its far edge, pauses, reverses, moves
back to the start, and pauses again, alternating direction on every cycle.
Timing uses elapsed milliseconds and a subpixel accumulator, the same technique
frame animation already uses; a late update jumps directly to the due integer
pixel offset rather than replaying intermediate frames, and 32-bit millisecond
wraparound is handled correctly. A display write is scheduled only when the
visible integer pixel offset actually changes.

Changing the expanded text, or a binding that affects `size`, `scroll.width`, or
`scroll.speed`, resets scroll position, phase, and direction to their initial
state and restarts the pause. Any other change — an unrelated data value, a
binding affecting `x` or color, or calling the engine's notification-recovery
`invalidate()` — does not reset scroll progress; switching themes does.

### Image

```json
{"id":"background","type":"image","x":0,"y":0,"source":"images/background.png"}
```

Dimensions come from the asset, between 1×1 and 240×240. No runtime scaling.
PNG alpha is preserved and composited against the layers below it.

### Shape

```json
{"id":"box","type":"shape","shape":"rectangle","x":20,"y":20,"width":200,"height":60,"cornerRadius":8,"fill":"#102030","stroke":"#ffffff","strokeWidth":2}
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

A rectangle's `cornerRadius` is optional, defaults to 0 (square corners), and
has a static manifest range of 0–120 px. After any bindings resolve, the
effective radius is clamped to `floor(min(width, height) / 2)`, so a rectangle
that shrinks (through a bound `width` or `height`) never draws rounding outside
its own bounds. Fill occupies the rounded outer shape; stroke occupies the ring
between that outer shape and an inner shape whose radius is
`max(0, cornerRadius - strokeWidth)`. A dynamic width or height that resolves to
zero makes the rectangle temporarily invisible, regardless of `cornerRadius`.

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

## Dynamic bindings

`bind` is an optional object on any layer, keyed by a supported property name.
Each key holds exactly one numeric mapping or color-stop binding, driven by a
fetched scalar. A binding's `source` is the same declared
`<data-source-id>.<field-id>` token accepted by text interpolation. Bindings are
recomputed from the parsed theme and the latest fetched values whenever those
values change; the parsed manifest itself is never mutated. Only the changed
region is repainted — the union of the layer's old and new visible bounds.

### Numeric mapping

```json
{"bind": {"width": {"source": "status.level", "input": [0, 100], "output": [0, 200], "clamp": true}}}
```

| Field | Type | Required | Rule |
| --- | --- | --- | --- |
| `source` | string | yes | Must reference a declared data field |
| `input` | two finite numbers | yes | Endpoints must differ; ascending or descending is allowed |
| `output` | two integers | yes | Both must fit the target property's safe range below |
| `clamp` | boolean | no, default true | Clamp to the output endpoints when true |

The mapping is linear:

```text
output0 + (source - input0) * (output1 - output0) / (input1 - input0)
```

The result is rounded to the nearest integer, halves away from zero. When
`clamp` is true, the source value is limited to the interval between the two
`input` endpoints before interpolation. When `clamp` is false, extrapolation
beyond that interval is permitted, but the final integer is still clamped to
the property's safe range below — that clamp cannot be disabled. A missing,
partial, or non-finite fetched value leaves the static manifest value in
effect for that property, so a theme always has a valid fallback appearance.

| Property | Safe range |
| --- | --- |
| `x`, `y`, `x2`, `y2` | −240 through 479 |
| `width`, `height`, `radius` | 0–240 |
| `cornerRadius` | 0–120, then limited to half the resolved rectangle size |
| `size` | 8–96 |
| `strokeWidth` | 1–32 |
| `scroll.width` | 1–240 |
| `scroll.speed` | 1–240 |

### Color stops

```json
{"bind": {"fill": {"source": "status.level",
  "stops": [{"at": 0, "value": "#35c46a"}, {"at": 60, "value": "#f0b429"}, {"at": 80, "value": "#e5484d"}]}}}
```

| Field | Type | Required | Rule |
| --- | --- | --- | --- |
| `source` | string | yes | Must reference a declared data field |
| `stops` | array | yes | 1–8 entries |
| `stops[].at` | finite number | yes | Strictly increasing |
| `stops[].value` | `#RRGGBB` | yes | Existing color syntax |

The selected color is the last stop whose `at` is less than or equal to the
fetched value; a value below the first stop's `at` uses the first stop's color.
Every finite fetched number therefore has a color, with no separate default
branch. A missing or non-finite fetched value leaves the static manifest color
in effect.

### Property matrix

Each property may be bound once; a layer may bind at most eight properties. A
color property may be bound only when that property already exists statically
on the layer — binding `stroke` does not implicitly enable a missing stroke,
and `scroll.width`/`scroll.speed` require a static `scroll` object. Image and
animation dimensions are not bindable, because runtime asset scaling is
unsupported and their declared size must match packaged assets.

| Layer | Numeric properties | Color properties |
| --- | --- | --- |
| text | `x`, `y`, `size`, `scroll.width`, `scroll.speed` | `color` |
| rectangle | `x`, `y`, `width`, `height`, `cornerRadius`, `strokeWidth` | `fill`, `stroke` |
| circle | `x`, `y`, `radius`, `strokeWidth` | `fill`, `stroke` |
| line | `x`, `y`, `x2`, `y2`, `strokeWidth` | `stroke` |
| image | `x`, `y` | none |
| animation | `x`, `y` | none |

An inapplicable property, a duplicate binding target, an unknown binding key,
or a source that is not a declared data field rejects the package, for example
`layers[0].bind.width: binding is not applicable to this layer` or
`layers[0].bind.width.source: expected a declared data field`. Invalid `scroll`
fields are diagnosed the same way, for example `layers[0].scroll.mode: expected
loop or bounce` or `layers[0].scroll.gap: not allowed in bounce mode`.

## Example: a dynamic dashboard

`examples/themes/live-status` builds `examples/themes/live-status.stheme` from a
theme declaring one data source (`status`, with `label`, `level`, and `state`
fields) and every capability above:

* `headline` — a `loop`-scrolling `{status.label}` text viewport;
* `headline-bounce` — a second, `bounce`-scrolling label, so both scroll modes
  are copyable side by side;
* `level-track` and `level-bar` — a rounded track and a bar whose `width` maps
  fetched level 0–100 to 0–200 px, with `fill` selected from green/amber/red
  color stops at the same thresholds;
* `level-caption` — status text whose `color` is bound from the same numeric
  `status.level` field, through color stops rather than a numeric mapping.

Every layer also has valid static fallback geometry and colors, so the theme
produces a correct first frame even before any fetch completes. Rebuild and
preview it with the `--data` workflow documented above.

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
animations invalidate only when their visible frame changes. Scrolling text
schedules a write only when its visible integer pixel offset changes, and
bindings are recomputed, and repainted, only when their fetched values actually
change. Dirty areas are recomposited through all intersecting layers, restoring
old glyphs, transparent sprite pixels, and foreground layers correctly, and a
scrolling layer's glyphs stay clipped to its own viewport even under a wider
dirty region from another layer.

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
No padding, trailing bytes, executable entries, or external asset references are
accepted. JSON data-source URLs remain allowed through the manifest's `data` block.

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

After resolving PlatformIO's dependencies:

```bash
sh tests/theme/run.sh
python3 -m unittest discover -s tests/theme -p 'test_*.py'
node tests/theme/test_webui.js
pio run -e smalltv -e smalltv_c2 -e smalltv_esp32 -e smalltv_esp32_wg -e smalltv_esp32_8mb -e smalltv_lean
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
