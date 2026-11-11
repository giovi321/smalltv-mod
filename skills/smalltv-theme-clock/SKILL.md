---
name: smalltv-theme-clock
description: Design and build custom theme clocks for smalltv-mod (GeekMagic SmallTV custom firmware) and output an installable .stheme package with a PNG/GIF preview. Use when the user wants a SmallTV clock face, theme clock, .stheme file, analog or digital watch face for the SmallTV, or wants to change, recolor, preview or install an existing theme.
---

# SmallTV theme clocks

A theme clock is a declarative 240×240 clock face: a `theme.json` manifest plus PNG assets,
packed into a binary `.stheme` file that the smalltv-mod firmware installs from
**Display → Theme clocks**. A theme contains data only, never code.

Everything runs through one script, `scripts/stheme.py` in this skill's folder (Python 3.9+
and Pillow; no compiler, no PlatformIO, no network). It validates with the same rules as the
firmware and SmallTV Studio, packs byte-identical to the firmware's own packer, and renders
previews with the firmware's integer geometry, RGB565 colors and bitmap font.

```
S=<this skill folder>/scripts/stheme.py
python3 $S scaffold my-clock --name "My Clock" --author "Name"   # default digital face
python3 $S validate my-clock                                     # or a .stheme
python3 $S build my-clock my-clock.stheme
python3 $S preview my-clock.stheme my-clock --time 2026-11-06T10:09:55 --seconds 6
python3 $S image photo.jpg my-clock/images/background.png --size 240x240 --fit cover
python3 $S gif cat.gif my-clock/animations/cat --size 64x64 --pixel
python3 $S install my-clock.stheme --host 192.168.1.50 --activate   # opt-in only
python3 $S palette night
```

Before writing a manifest, read the themes page of the smalltv-mod docs for every layer
type, field, token, binding and limit, and `references/format.md` for what the docs do not
cover (`"spec": "auto"`, the spec 2 features, tested analog hand recipes); it links each
docs section. `stheme.py validate` is the source of truth when the two disagree. Read `references/design.md` for layout math and the palette, and
`references/data-sources.md` only when the clock shows fetched data.

## Workflow

### 1. Short interview (at most 5 questions, one round)

Ask only what the request leaves open, in one message (or one multiple-choice tool call if
available). Skip any question the user already answered. Typical questions:

1. Style: digital (default), analog dial, or a mix; any vibe or reference (minimal, retro
   terminal, pixel art, photo background)?
2. Time format: 24-hour `{HH}:{MM}` (default) or 12-hour `{hh}:{MM} {AMPM}`; seconds?
3. Date: weekday + date (default), date only, none. Device language other than English?
4. Extras: live data (weather, a JSON endpoint), a seconds ring/bar, moon phase, an animation
   or image (their own file, or one you draw)?
5. Colors: the default **SmallTV Night** palette, or their own colors?

If the user says "just make it" or gives a full brief, skip the interview and use the
defaults. When working unattended, use the defaults and say which you assumed.

### 2. Defaults (when the user does not say otherwise)

- Digital layout, 24-hour `{HH}:{MM}`, weekday and date shown, no seconds.
- Palette **SmallTV Night** (`python3 $S palette night`):
  background `#0b1420`, surface `#16202c`, line `#233145`, text `#e9edf4`, muted `#7f93ab`,
  accent `#47c2ff`, ok/warn/bad `#35c46a` / `#f0b429` / `#e5484d`.
  A user palette replaces these roles one-for-one; keep the roles (background, surface,
  text, muted, accent) so the layout still reads.
- `"spec": "auto"` in the source. `build` resolves it to **1** unless the theme uses a
  spec 2-only feature (arc/hand/ticks shapes, `time.*`/`device.*` bindings, `{H}` `{MO}`
  `{AMPM}` `{WW}`, `{device.*}`, per-language text), then **2**, and writes the number back to
  `theme.json` so the firmware tools accept the folder. Spec 1 themes also open in SmallTV
  Studio; spec 2 needs firmware 3.4.0+ and Studio cannot edit it yet. If the user asks for
  localized month/day names, set `"spec": 2` explicitly (a spec 1 theme always shows English).
- `theme.id`: lowercase slug of the name (letters, digits, `-`, `_`, max 48). `version`
  `1.0.0`. `author`: the user's name if known, otherwise ask or use "SmallTV theme skill".

### 3. Design and write the source folder

```
my-clock/
├── theme.json
├── images/…png            (optional, ≤ 240×240 each)
└── animations/<name>/000.png 001.png …   (optional, all the same size)
```

Start from `scaffold` for digital faces, or write `theme.json` directly. Rules that matter:

- Layers draw in array order, back to front. Max 32 layers, unique IDs.
- Text is the built-in 5×8 ASCII bitmap font, scaled: `size` is the cell height (8–96),
  cell width `ceil(size*6/8)`, text width = characters × cell width. Printable ASCII only:
  no `°`, `€`, accents or emoji (draw a small circle for a degree sign). Sizes that are
  multiples of 8 scale cleanly. Do the width math so nothing is clipped; see `design.md`.
- Leave room for `--`: every clock token shows `--` before the first NTP sync and every
  data field shows `--` until fetched.
- Analog faces: `ticks` for the dial, `hand` layers bound to `time.*` sources, `arc` for
  progress rings. Angles are degrees, 0 at 12 o'clock, clockwise. See the hand recipes in
  `format.md` for the exact bindings.
- Raster art: use the user's image via `image`/`gif` commands, or draw procedural art with
  Pillow (gradients, patterns, pixel sprites; see `design.md`). Save PNGs into the folder and
  reference them by relative `source` path. No runtime scaling exists: export at final size.
- Fetched data: follow `data-sources.md`. Every bound layer needs sensible static values so
  the first frame is correct before any fetch.

### 4. Validate, build, preview, look

```
python3 $S build my-clock out/my-clock.stheme
python3 $S preview out/my-clock.stheme out/my-clock --time <a telling time> --seconds 6
```

- Pick a preview time that exercises the layout: a wide one like `23:58:55` on a Wednesday
  in September for the longest names, plus `--unsynced` for the first-boot look. Use
  `--data src.field=value` for fetched fields and `--lang de` etc. for translations.
- **Look at the PNG yourself** (open/view the image) before showing it. Check clipping,
  overlaps, contrast, centering and the `--` state. Fix and rebuild until it is right.
- Validation errors name the exact field (`layers[3].bind.sweep: binding is not applicable to this layer`); fix the
  manifest and rerun. Never hand-edit a `.stheme`.

### 5. Deliver

Give the user the `.stheme`, the preview PNG and GIF, and the source folder (zip it when the
environment delivers single files). State the spec it built as and the firmware need
(spec 2 → smalltv-mod 3.4.0+). Install: **Display → Theme clocks → Install .stheme → Use
theme**, and set the timezone under **Clock & night mode**. The source folder also rebuilds
with the firmware's `tools/smalltv_theme.py build` and, for spec 1, imports into
[SmallTV Studio](https://giovi321.github.io/smalltv-studio/).

Then iterate on feedback: change `theme.json`, rebuild, re-preview, re-check the PNG.

### 6. Optional: install on the TV

Only when the user gives the TV's IP (or hostname) and the agent can reach that network
(local CLI agents, not cloud chat sandboxes). Ask before activating.

```
python3 $S install out/my-clock.stheme --host 192.168.1.50 [--password …] [--activate] [--replace]
```

It uses the firmware API (`/api/themes`, `/install`, `/select`, `/delete`) with the
optional digest password (user `admin` by default). The active theme cannot be replaced:
the user switches the TV to another mode first. Installing a theme also installs its data
sources: the TV will poll those URLs, so only install themes the user trusts.

## Hard limits (quick list)

240×240 canvas · 32 layers · manifest 16 KiB · package 3 MiB and 256 entries · 16 installed
themes · text 1–128 printable ASCII bytes · up to 4 data sources × 8 fields, response
≤ 2 KiB, interval ≥ 10 s · animation 1–240 frames, 1–15 FPS · up to 8 bindings per layer ·
colors exactly `#RRGGBB`. No expressions, conditions, arrays, history or custom fonts.
