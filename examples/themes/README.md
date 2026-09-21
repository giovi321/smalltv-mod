# Source themes

Two examples are ready to install from **Display → Theme clocks** on SmallTV Pro:

* [`pixel-room.stheme`](pixel-room.stheme) — pixel-art room with an animated cat;
* [`terminal-ops.stheme`](terminal-ops.stheme) — dense text and line dashboard.

Their editable sources are in the matching directories. Open
[pixel-room-preview.html](pixel-room-preview.html) or
[terminal-ops-preview.html](terminal-ops-preview.html) for an offline preview.
SmallTV Studio is maintained separately from this firmware repository.

Validate/rebuild/preview (Python 3.10+, Pillow, C++11 compiler and the project's
ArduinoJson/GFX headers; see the guide below for setup):

```sh
python3 tools/smalltv_theme.py validate examples/themes/pixel-room
python3 tools/smalltv_theme.py build examples/themes/pixel-room examples/themes/pixel-room.stheme
python3 tools/smalltv_theme.py preview examples/themes/pixel-room examples/themes/pixel-room-preview.html --time 2026-09-18T10:24:55 --seconds 10 --fps 15
```

The original geometric PNGs can be reproduced with
`python3 examples/themes/make_pixel_room_assets.py`. The desktop script is not
included in the installed package; the theme contains declarative data only.
Terminal Ops can be recreated with `python3 tools/create_terminal_theme.py`.


See the [format and authoring guide](../../docs/src/content/docs/features/themes.md)
for layer fields, limits, binary formats, and installation APIs.
