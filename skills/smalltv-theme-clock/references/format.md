# theme.json reference (smalltv-mod Theme Engine, spec 1 and 2)

Condensed from smalltv-mod `docs/src/content/docs/features/themes.md`
(<https://giovi321.github.io/smalltv-mod/features/themes/>). `stheme.py validate` enforces all
of it. **(2)** marks spec 2-only features (firmware 3.4.0+).

## Root

```json
{
  "spec": "auto",
  "theme": {"id": "minimal-clock", "name": "Minimal Clock", "author": "You", "version": "1.0.0"},
  "display": {"width": 240, "height": 240, "background": "#000000"},
  "data": [],
  "layers": []
}
```

- `spec`: `1`, `2`, or `"auto"` (skill only; `build` replaces it with the number).
- `theme.id` 1–48 of `A-Z a-z 0-9 _ -`; `name`/`author` ≤ 96 bytes; `version` ≤ 32 bytes.
- `display` is always 240×240; origin top-left; integer pixels.
- `data` optional (see `data-sources.md`). Unknown fields anywhere are rejected.

Every layer needs `id`, `type`, `x`, `y` (−240…479, clipped to the canvas) and may have
`bind` (any layer) and `scroll` (text only).

## Text

```json
{"id": "clock", "type": "text", "x": 120, "y": 100, "anchor": "center",
 "value": "{HH}:{MM}", "size": 48, "color": "#ffffff"}
```

- `size` 8–96 = cell height; cell width `ceil(size*6/8)`; one line, no wrapping.
- `anchor` (default `top-left`): `top-left top-center top-right center-left center
  center-right bottom-left bottom-center bottom-right`, positioning the whole text box
  (or the scroll viewport).
- `value`: 1–128 printable ASCII bytes. `{...}` must be a token below or a declared
  `source.field`. A stray `}` is an error.
- **(2)** `value` may be an object per language: `{"en": "Today", "de": "Heute", "fr": "…"}`.
  Keys `en pt es fr de it`; `en` required (fallback). All non-English texts of a theme share
  1,024 bytes of device RAM: 28 bytes each, plus length+1 when longer than 15 characters.

| Token | Value | | Token **(2)** | Value |
|---|---|---|---|---|
| `{HH}` | 00–23 | | `{H}` | 0–23, no leading zero |
| `{hh}` | 01–12 | | `{MO}` | 01–12 month number |
| `{MM}` `{SS}` | minute, second | | `{AMPM}` | `AM` / `PM` |
| `{DD}` | 01–31 | | `{device.temperature}` etc. | firmware value (see built-ins) |
| `{MON}` / `{MONTH}` | Nov / November | | | |
| `{WD}` / `{WEEKDAY}` | Fri / Friday | | | |
| `{YYYY}` | year | | | |

Names are English in spec 1; in spec 2 they follow the device language (`en pt es fr de it`,
ASCII only: `Maerz`, `Miercoles`, `Fevrier`; abbreviations from tables, e.g. French `Juil`,
`Sept`, German `Mit`, `Don`). Before NTP sync every clock token is `--`.

### Scrolling (text only)

```json
"scroll": {"width": 200, "mode": "loop", "speed": 30, "pause": 1200, "gap": 24}
```

`width` 1–240 (viewport; height = `size`), `mode` `loop`|`bounce`, `speed` 1–240 px/s,
`pause` 0–10000 ms (default 1000), `gap` 0–240 (loop only, default 24; an error in bounce).
Text that fits the viewport does not move. A change of the expanded text restarts the scroll,
so do not put `{SS}` in a scrolling layer.

## Image

```json
{"id": "background", "type": "image", "x": 0, "y": 0, "source": "images/background.png"}
```

Size comes from the PNG (1×1…240×240), alpha supported, no scaling.

## Animation

```json
{"id": "cat", "type": "animation", "x": 160, "y": 170, "width": 64, "height": 64,
 "source": "animations/cat", "frames": 12, "fps": 8, "loop": true}
```

All fields required. Frames `000.png`, `001.png`, … exactly `width`×`height`. 1–240 frames,
1–15 FPS. `loop: false` holds the last frame. Each frame costs storage (≈ w·h·2–3 bytes in
spec 1, often much less in spec 2), so keep sprites small.

## Shapes

```json
{"id": "box", "type": "shape", "shape": "rectangle", "x": 20, "y": 20, "width": 200, "height": 60,
 "cornerRadius": 8, "fill": "#102030", "stroke": "#ffffff", "strokeWidth": 2}
{"id": "ring", "type": "shape", "shape": "circle", "x": 120, "y": 120, "radius": 110, "stroke": "#ffffff", "strokeWidth": 2}
{"id": "line", "type": "shape", "shape": "line", "x": 20, "y": 180, "x2": 220, "y2": 180, "stroke": "#ffffff", "strokeWidth": 2}
```

- `strokeWidth` 1–32, default 1. Rectangle/circle need `fill`, `stroke` or both; a line
  needs `stroke`. Strokes of rectangles and circles sit inside the outline; lines have round
  ends. Rectangle `width`/`height` 1–240, `cornerRadius` 0–120 (clamped to half the side).
  Circle `x`,`y` is the center, `radius` 1–240.

**(2)** Rotation shapes, drawn in `stroke` only (`fill` is an unknown field):

```json
{"id": "progress", "type": "shape", "shape": "arc", "x": 120, "y": 120, "radius": 100, "start": 0, "sweep": 270, "stroke": "#47c2ff", "strokeWidth": 10}
{"id": "minute", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 90, "tail": 20, "angle": 0, "stroke": "#ffffff", "strokeWidth": 4}
{"id": "dial", "type": "shape", "shape": "ticks", "x": 120, "y": 120, "radius": 112, "count": 12, "length": 10, "stroke": "#8a8a8a", "strokeWidth": 3}
```

- Angles: whole degrees, 0 = 12 o'clock, clockwise; −360…360.
- `arc`: ring band `strokeWidth` wide inside `radius` (1–240), from `start` for `sweep`
  degrees (negative = counter-clockwise; ≥360 full ring; 0 draws nothing). `strokeWidth ≥
  radius` fills a wedge (but strokeWidth max is 32).
- `hand`: from `tail` (0–240, default 0) behind the pivot to `length` (1–240) in front.
- `ticks`: `count` (1–60) marks, the first at 12, each `length` (1–240) inward from `radius`.

## Bindings (`bind`)

One binding per property, at most 8 per layer. A missing or non-numeric value keeps the
static value, so always give good static values.

Numeric mapping (linear, rounded half away from zero, `clamp` default true; the result is
always clamped to the property's safe range):

```json
"bind": {"width": {"source": "status.level", "input": [0, 100], "output": [0, 200], "clamp": true}}
```

Color stops (last stop with `at` ≤ value; below the first, the first color; 1–8 stops,
strictly increasing `at`):

```json
"bind": {"fill": {"source": "status.level", "stops": [{"at": 0, "value": "#35c46a"}, {"at": 60, "value": "#f0b429"}, {"at": 80, "value": "#e5484d"}]}}
```

| Layer | Numeric targets | Color targets |
|---|---|---|
| text | `x y size scroll.width scroll.speed` | `color` |
| rectangle | `x y width height cornerRadius strokeWidth` | `fill stroke` |
| circle | `x y radius strokeWidth` | `fill stroke` |
| line | `x y x2 y2 strokeWidth` | `stroke` |
| arc **(2)** | `x y radius sweep strokeWidth` | `stroke` |
| hand **(2)** | `x y angle strokeWidth` | `stroke` |
| ticks **(2)** | `x y radius strokeWidth` | `stroke` |
| image, animation | `x y` | none |

Safe ranges: `x y x2 y2` −240…479; `width height radius` 0–240; `cornerRadius` 0–120;
`size` 8–96; `strokeWidth` 1–32; `scroll.*` 1–240; `sweep angle` −360…360. A color target
must already exist statically on the layer (binding `stroke` needs a static `stroke`).

### Built-in sources **(2)**

| Source | Range | | Source | Range |
|---|---|---|---|---|
| `time.second` | 0–59 | | `time.day` | 1–31 |
| `time.minute` | 0–59 | | `time.month` | 1–12 |
| `time.hour` | 0–23 | | `time.weekday` | 0–6, Sunday 0 |
| `time.hour12` | 0–11 | | `time.moon` | 0–29 moon age (0 new, 14–15 full) |
| `time.secondOfDay` | 0–86399 | | | |

`device.focusToday device.focusGoal device.streak device.humidity device.temperature` exist
for SmallTV Pomodoro compatibility; smalltv-mod supplies none of them today (they show `--`
and bindings keep static values). In spec 2, `time` and `device` cannot be data-source IDs.

### Analog hand recipes (tested against the firmware renderer)

```json
{"id": "second", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 95, "tail": 20, "angle": 0,
 "stroke": "#ff5a47", "strokeWidth": 1,
 "bind": {"angle": {"source": "time.second", "input": [0, 60], "output": [0, 360]}}}
{"id": "minute", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 85, "tail": 14, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 4,
 "bind": {"angle": {"source": "time.minute", "input": [0, 60], "output": [0, 360]}}}
```

The minute hand steps once a minute, the second hand once a second (that is the device
behavior; there is no sub-second source).

**Hour hand.** Outputs are capped at ±360, so one binding cannot cover 24 hours smoothly.
Simple, stepping once an hour:

```json
"bind": {"angle": {"source": "time.hour12", "input": [0, 12], "output": [0, 360]}}
```

Smooth all day: two hour hands, one for AM and one for PM, each moved off-screen (x bound to
`time.hour`) during the other half of the day:

```json
{"id": "hour-am", "type": "shape", "shape": "hand", "x": 120, "y": 120, "length": 55, "tail": 10, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 6,
 "bind": {"angle": {"source": "time.secondOfDay", "input": [0, 43200], "output": [0, 360]},
          "x": {"source": "time.hour", "input": [11, 12], "output": [120, 479]}}}
{"id": "hour-pm", "type": "shape", "shape": "hand", "x": -240, "y": 120, "length": 55, "tail": 10, "angle": 0,
 "stroke": "#e9edf4", "strokeWidth": 6,
 "bind": {"angle": {"source": "time.secondOfDay", "input": [43200, 86400], "output": [0, 360]},
          "x": {"source": "time.hour", "input": [11, 12], "output": [-240, 120]}}}
```

Before NTP sync there are no `time.*` values, so hands keep their static `angle` and `x`:
give the AM hand the on-screen static `x` (as above) so an unsynced dial still looks like a
clock. Draw hands after the dial and a small center cap (circle) last.

## Package format (for reference only; `build` writes it)

`STH1` container (uint16 entry count, entries of uint16 path length + uint32 size + path +
payload), one `theme.json` (compact JSON) and `.sti` images: `images/x.png` → `images/x.png.sti`,
animation frames `animations/cat/000.png.sti`. STI1 = raw RGB565 (+alpha); STI2 (spec 2 only)
= run-length rows, optional 16-color palette; the packer picks the smallest per image.

## HTTP API (used by `install`)

`GET /api/themes` · `POST /api/themes/install` (multipart `file`, 201) · `POST
/api/themes/select` `{"id"}` · `POST /api/themes/delete` `{"id"}`. Optional digest auth.
400 invalid package, 409 duplicate ID or active theme, 404 missing.
