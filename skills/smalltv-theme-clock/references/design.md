# Designing for the 240×240 SmallTV panel

## Palette: SmallTV Night (default)

| Role | Hex | Use |
|---|---|---|
| background | `#0b1420` | `display.background`, the canvas |
| surface | `#16202c` | cards, tracks, dial faces |
| line | `#233145` | dividers, card strokes, minor ticks |
| text | `#e9edf4` | the time, primary values |
| muted | `#7f93ab` | date, labels, units |
| accent | `#47c2ff` | one highlight: weekday, seconds ring, rule |
| ok / warn / bad | `#35c46a` / `#f0b429` / `#e5484d` | color stops for levels and states |

Other built-ins: `python3 stheme.py palette` (`terminal`, `mono`). When the user gives their
own colors, map them onto these roles; if they give fewer, derive the rest (muted ≈ text at
55% toward background, surface ≈ background lightened 5–8%). Keep contrast high: the panel is
small, viewed from a distance, and colors are quantized to RGB565 (5-6-5 bits), so very close
dark shades merge and subtle gradients band.

## Text math (built-in font only)

Cell width `ceil(size × 6 / 8)`; text width = characters × cell width; height = `size`.
Each glyph is 5 columns + 1 blank column of an 8-row bitmap, scaled, so the last character
carries one blank column on its right (centered text sits ~½ column left of center).

| size | cell w | "HH:MM" (5) | "HH:MM:SS" (8) | max chars in 240 px |
|---|---|---|---|---|
| 8 | 6 | 30 | 48 | 40 |
| 12 | 9 | 45 | 72 | 26 |
| 16 | 12 | 60 | 96 | 20 |
| 24 | 18 | 90 | 144 | 13 |
| 32 | 24 | 120 | 192 | 10 |
| 40 | 30 | 150 | 240 | 8 |
| 48 | 36 | 180 | 288 ✗ | 6 |
| 56 | 42 | 210 | ✗ | 5 |
| 64 | 48 | 240 | ✗ | 5 |
| 96 | 72 | ✗ | ✗ | 3 |

- Multiples of 8 (8, 16, 24, …) scale every font pixel evenly; other sizes look slightly
  uneven but are fine for large digits (e.g. 56).
- Plan for the longest content: `{WEEKDAY}` up to 9 chars in English (`Wednesday`), 13 in
  Portuguese (`Segunda-feira`); `{MONTH}` up to 9 (`September`) / 10 (`Septiembre`); data
  fields as long as their server sends; and `--` before sync. Use `scroll` for unbounded text.
- Uppercase reads better at small sizes; lowercase descenders fit inside the cell.

## Layout recipes

**Digital (default, = `stheme.py scaffold`)**: surface card 216×160 at (12,40) radius 16;
`{WEEKDAY}` size 16 accent at center (120,64); `{HH}:{MM}` size 56 text at center (120,112);
accent rule 60×3 at (90,146); `{DD} {MON} {YYYY}` size 16 muted at center (120,172).

**Digital with seconds**: `{HH}:{MM}` size 48 at (120,104) plus `{SS}` size 16 accent to the
right, or a seconds bar: rectangle `width` bound to `time.second` `[0,59]→[0,200]` (spec 2).

**Analog dial (spec 2)**: background circle r=118 surface; `ticks` r=112 count 60 length 4
line color; `ticks` r=112 count 12 length 12 strokeWidth 3 text color; optional `arc`
seconds ring r=104 strokeWidth 4 accent bound to `time.second` `[0,60]→[0,360]`; date text
inside the dial at y≈160; hands (hour, minute, second) per the hand recipes in `format.md`; center cap circle r=5
accent last. Optional small digital `{HH}:{MM}` size 16 at y≈80.

**Data dashboard**: title size 12 muted; values size 24–32 text; a rounded track rectangle
(surface) with a bar on top whose `width` and `fill` are bound to the value; a status text
whose `color` uses the same color stops. Static fallbacks: bar width 1, colors = ok.

**Photo / art background**: a 240×240 PNG at (0,0) as layer 0; put text on a semi-opaque
surface panel (draw it into the PNG with alpha, since shape fills are opaque) or pick text
colors that contrast with the image region underneath.

## Generating art with Pillow

Draw at final pixel size and save PNG into the theme folder; reference it by relative path.

- Gradients and vignettes: draw concentric shapes or per-row colors; RGB565 shows bands
  every ~8 levels of red/blue, so prefer bold steps or a slight dither.
- Pixel art: draw at 1/4 or 1/8 scale, then upscale with `Image.NEAREST`
  (`stheme.py image in.png out.png --size 240x240 --fit stretch --pixel`).
- Sprites/animations: one RGBA PNG per frame, identical size, transparent background, frames
  named `000.png`…; 4–12 frames at 4–10 FPS is plenty and keeps the package small.
- Few-color images (≤16 colors) and large transparent areas compress very well in spec 2
  (STI2); spec 1 stores every pixel at 2–3 bytes (a full-screen opaque image is 115 KB).
- The firmware's `examples/themes/make_pixel_room_assets.py` is a worked example.

User images: `stheme.py image photo.jpg images/bg.png --size 240x240 --fit cover` (crop),
`--fit contain` (letterbox with transparency), `--pixel` for pixel art. GIFs:
`stheme.py gif in.gif animations/name --size 64x64` writes frames and prints the animation
layer fields (`source`, `width`, `height`, `frames`, `fps`) to paste into `theme.json`.

## Checklist before delivering

- [ ] Preview PNG viewed: nothing clipped at the longest weekday/month and at `--`.
- [ ] Text readable: ≥ size 12 for secondary info, contrast against what is behind it.
- [ ] Every bound layer has sensible static values (unsynced and no-data previews look right).
- [ ] Layer order: background → panels → dial/ticks → text → hands → caps/foreground.
- [ ] Package size reasonable (`build` prints it); animations and full-screen images dominate.
