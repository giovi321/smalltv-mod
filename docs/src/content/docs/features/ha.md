---
title: Home Assistant screens
description: Full screens pushed from Home Assistant over MQTT as small JSON draw lists, one retained message per slot, rotating in the device carousel.
---

The other features are things the device fetches itself. This one is the opposite: you publish a small JSON document to an MQTT topic, and the device draws it as a full 240×240 screen and keeps it in the carousel. A temperature warning, a door-left-open reminder, the day's energy total. Anything Home Assistant can template, the panel can show.

This feature is not in `smalltv-mod-firmware-lean.bin`. That ESP8266 image compiles it out to free 7,668 bytes of heap, and the web UI hides the tab when it is missing. Use `smalltv-mod-firmware.bin` on an ESP8266 that needs HA screens, or any of the ESP32 images.

Because every screen is a retained message, the broker holds the last copy. The device can reboot, Home Assistant can reboot, and the screens come back on their own.

Brightness is on MQTT too, with its own topics and Home Assistant auto-discovery; see [Brightness over MQTT](#brightness-over-mqtt).

## Pointing the device at your broker

Open the settings page and find the MQTT card. Enter your broker's host, port (1883 by default), and an optional username and password, then save. The connection is plain TCP, not TLS, so keep the broker on your LAN.

The device identifies itself by its hostname, the same name you see in the web UI and browse to as `<hostname>.local`. That name appears in every topic below; the examples use `smalltv`.

## The MQTT contract

| Topic | Direction | What it carries |
|---|---|---|
| `smalltv/<hostname>/availability` | device → broker | `online`, retained, when the device connects. The broker holds a retained `offline` as the device's last will, so the topic always tells you whether the panel is really there. |
| `smalltv/<hostname>/screen/<slot>` | broker → device | One screen, as retained JSON. `<slot>` is a name you pick, such as `window` or `energy`. |
| `smalltv/<hostname>/brightness` | device → broker | Panel brightness 0-100 as a plain integer, retained. Published on connect and on every change, including changes made in the web UI. |
| `smalltv/<hostname>/brightness/set` | broker → device | Set brightness. Plain integer 0-100, not JSON. Values are clamped, and anything that is not a number is ignored. Publish without retain. |

Screen messages must be published with the retain flag. This is not optional: retain is what lets a screen survive a device reboot, and what makes a screen published while the device was off appear when it comes back.

Publishing an **empty retained payload** to a screen topic deletes that slot, from both the broker and the device.

When several slots are set, the device rotates through them in slot order, dwelling on each for the time configured on the device (15 seconds by default). A screen whose `ttl` has expired drops out of the rotation without anything needing to republish. On a board with a touch button, a short press set to *Next page* steps to the next screen when the device is in Home Assistant mode, and the dwell timer restarts on it.

### Limits

Keep payloads small. The numbers that matter:

| Limit | ESP32 builds | ESP8266 build |
|---|---|---|
| Payload size | ~2 KB | ~700 B |
| Screens (slots) | 8 | 4 |
| Draw primitives per screen | 48 | 24 |
| Bitmap width and height | 48 px | 32 px |

The limits above apply to screens. Brightness messages are a few bytes and always fit.

## Brightness over MQTT

The panel's brightness is on the wire too. The device publishes it as a plain integer from 0 to 100 on `smalltv/<hostname>/brightness`, retained, on connect and on every change, including changes you make in the device's web UI. Home Assistant and the web UI read the same topic, so they stay in sync.

With discovery enabled in Home Assistant's MQTT integration (the default), a Brightness number entity appears on its own, no YAML needed. The device publishes the discovery config as a retained message under the `homeassistant` prefix, and it re-publishes the config and the state on its own after a Home Assistant restart.

To set brightness, publish a plain integer to the command topic. It is not JSON: send `70`, not `{"value":70}`. Out-of-range values are clamped to 0-100, and a payload that is not a number is ignored. Do not set the retain flag on the command; only the state topic is retained.

```bash
mosquitto_pub -h broker.local -t smalltv/smalltv/brightness/set -m 70

# watch the state topic
mosquitto_sub -h broker.local -t smalltv/smalltv/brightness
```

The change shows on the panel at once. Saving to flash is debounced by a few seconds, so rapid changes do not wear out the flash. If the web UI is open in a browser while brightness changes over MQTT, the open page does not live-refresh; reload it to see the new value.

With discovery disabled, declare the number by hand:

```yaml
mqtt:
  number:
    - name: SmallTV brightness
      command_topic: smalltv/<hostname>/brightness/set
      state_topic: smalltv/<hostname>/brightness
      min: 0
      max: 100
```

## Drawing on the screen

A screen is a JSON object with an optional background colour, an optional time-to-live, and a list of draw primitives, painted in order onto the 240×240 panel:

```json
{
  "bg": "#00AA00",
  "ttl": 0,
  "draw": [
    {"t":"fill","c":"#003300"},
    {"t":"rect","x":10,"y":10,"w":220,"h":60,"c":"#FFFFFF","r":8},
    {"t":"circle","x":120,"y":120,"r":40,"c":"#FF0000"},
    {"t":"line","x":0,"y":200,"x2":240,"y2":200,"c":"#888888"},
    {"t":"icon","x":120,"y":130,"s":2,"c":"#FFCC00","a":"c","v":"sun"},
    {"t":"text","x":120,"y":60,"s":2,"c":"#FFFFFF","a":"c","v":"Open the window"},
    {"t":"text","x":120,"y":120,"s":3,"c":"#FFFF00","a":"c","v":"21.5 in / 18.2 out"}
  ]
}
```

| Field | Meaning |
|---|---|
| `bg` | Background colour, default black. |
| `ttl` | Seconds before the screen drops out of the rotation. `0` means sticky: it stays until you delete or replace it. |
| `draw` | List of primitives, painted in order. |

The primitives:

| `t` | Fields | Draws |
|---|---|---|
| `fill` | `c` | Flood the whole panel with a colour. |
| `rect` | `x`, `y`, `w`, `h`, `c` | A rectangle. |
| `rrect` | `x`, `y`, `w`, `h`, `c`, `r` | A rectangle with corner radius `r`. |
| `circle` | `x`, `y`, `r`, `c` | A circle at centre `x`,`y`. |
| `line` | `x`, `y`, `x2`, `y2`, `c` | A straight line. |
| `text` | `x`, `y`, `s`, `c`, `a`, `v` | A string in the built-in 6×8 font. |
| `icon` | `x`, `y`, `s`, `c`, `a`, `v` | A built-in icon; `v` is the icon name. |
| `bitmap` | `x`, `y`, `w`, `h`, `c`, `a`, `d`, `s` | A 1-bit bitmap; `d` is the pixel data as a hex string. |

Colours are `#RRGGBB`. Text `s` is an integer scale of the 6×8 font, and `a` aligns the string left (`l`), centre (`c`), or right (`r`) around `x`. Text accepts UTF-8. Beyond printable ASCII, the font covers these characters: `° ± ² µ · ÷ ¡ ¢ £ ¥ ª º « » ¬ ¼ ½ ¿ Ä Å Æ Ç É Ñ Ö Ü ß à á â ä å æ ç è é ê ë ì í î ï ñ ò ó ô ö ù ú û ü ÿ`. Anything without a glyph (`³` and `×` included) is silently dropped. The string is capped at 64 bytes after translation; a character like `°` costs one byte.

For `icon`, `s` is a 1-8 scale and the icon occupies a 24×`s` px box; `x` is the left/centre/right edge of that box according to `a` (default `l`), and `y` is its top edge. The icon names are: `thermometer`, `humidity`, `sun`, `cloud`, `rain`, `snow`, `wind`, `home`, `door-open`, `door-closed`, `window-open`, `window-closed`, `motion`, `plug`, `battery`, `alert`, `check`, `x`, `arrow-up`, `arrow-down`. An unknown name is skipped like any other malformed primitive.

For `bitmap`, `d` carries the pixels as a hex string: 1 bit per pixel, row-major, MSB-first within each byte. A set bit draws in colour `c`; an unset bit is transparent and leaves whatever was painted underneath. The bitmap occupies a `w`×`h` box, `a` anchors `x` to the left, centre, or right of that box, and `y` is the top edge. The string must be exactly ceil(`w`×`h`/8) bytes, two hex chars per byte: a 24×24 bitmap is 72 bytes and 144 hex chars, and a 2×2 bitmap with only the top-left pixel set is `"8000"`. Width and height cap at 48 on the ESP32 builds and 32 on the ESP8266, and an oversized or malformed bitmap is skipped like any other bad primitive. Watch the payload budget: a 48×48 bitmap alone is 576 hex chars, so it only fits an ESP32-family payload.

The optional `s` field scales the bitmap on the device by an integer factor from 1 to 4, default 1. Pixels are duplicated nearest-neighbour style, so the rendered size is `w`×`s` by `h`×`s`, and the anchor applies to that rendered box. A 24×24 source with `"s":2` draws 48×48 while the payload still carries only the 144 hex chars of the small source. The rendered size must stay at 240 px or less in each dimension, or the firmware skips the primitive.

Parsing is forgiving in one direction. Unknown fields are ignored, so you can add your own annotations. A malformed primitive is skipped and the rest of the list still draws. But a payload that is not valid JSON at all leaves the slot exactly as it was, so a templating mistake in Home Assistant cannot blank a working screen.

### Custom bitmaps (MDI icons)

The built-in icon set is small. The whole Material Design Icons catalogue works too, with one extra step: MDI ships as SVG paths and the device has no font or SVG rasteriser, so the conversion happens on your machine. The `tools/mdi_to_hex.py` script in the repository rasterises any MDI icon to the hex string the `bitmap` primitive expects:

```bash
python tools/mdi_to_hex.py window-open --size 24
```

It prints the raw hex plus a ready-made JSON fragment for a screen payload. Paste the hex into the bitmap field of the screen board blueprint, where it replaces the icon and draws in the foreground colour, or build the primitive yourself from the fragment. The script needs two Python packages (`svg.path` and `Pillow`); install them with `pip install -r tools/requirements-mdi.txt` into a virtualenv, not globally.

Sizes 24 and 32 stay under the 700 B ESP8266 payload limit. A 48 px bitmap pushes a screen payload past it and only works on the ESP32 builds; on an ESP8266 the firmware ignores the oversized message. Prefer a 24 px source with `"s":2` over generating a large bitmap: you get the same 48 px result at a quarter of the payload size. Whatever size you pick, the hex length must match it (144 chars for 24 px, 256 for 32 px, 576 for 48 px); a size and hex mismatch makes the firmware silently skip the bitmap, so regenerate the hex at the same size you put in `w` and `h`.

## Installing a blueprint

The three blueprints below live in the repository and install the same way.

The easy route, straight from the Home Assistant UI:

1. Open **Settings → Automations & scenes → Blueprints → Import blueprint**.
2. Paste the blueprint's GitHub URL, for example `https://github.com/giovi321/smalltv-mod/blob/main/blueprints/automation/smalltv/screen_board.yaml`. Home Assistant downloads and validates it, and tells you if anything is wrong.
3. Open the blueprint and pick **Create automation**, fill in the inputs, save.

The manual route, if you prefer files or run an older Home Assistant:

1. Copy the YAML file from [`blueprints/automation/smalltv/`](https://github.com/giovi321/smalltv-mod/tree/main/blueprints/automation/smalltv/) to `config/blueprints/automation/smalltv/` on your Home Assistant host. The `automation` folder in that path is required; without it the blueprint is ignored.
2. Go to **Developer tools → YAML → Reload automations**. The blueprint shows up under **Settings → Blueprints**.

Two things to know when updating to a newer version of a blueprint:

- Replacing the file is not enough on its own. Reload automations afterwards, and hard-refresh your browser (Ctrl+F5); the frontend caches the blueprint's fields, so new inputs stay invisible until you do.
- Automations you already created keep working. New inputs appear with their defaults the next time you edit and save the automation.

## The window screen from Home Assistant

The typical use: compare indoor and outdoor temperature and show a green or red full-screen answer to "should I open the window?". One automation, one `mqtt.publish`:

```yaml
automation:
  - alias: SmallTV window screen
    mode: restart
    triggers:
      - trigger: state
        entity_id: sensor.living_room_temperature
      - trigger: state
        entity_id: sensor.outdoor_temperature
      - trigger: time_pattern
        minutes: "/5"   # safety refresh, in case a change was missed
      - trigger: homeassistant
        event: start    # republish the retained screen after an HA restart
    conditions:
      - condition: template
        value_template: >-
          {{ states('sensor.living_room_temperature') not in ('unknown', 'unavailable')
             and states('sensor.outdoor_temperature') not in ('unknown', 'unavailable') }}
    actions:
      - action: mqtt.publish
        data:
          topic: smalltv/smalltv/screen/window
          retain: true
          payload: >-
            {% set ind = states('sensor.living_room_temperature') | float(0) -%}
            {% set out = states('sensor.outdoor_temperature') | float(0) -%}
            {% set open = out <= ind - 0.5 -%}
            {
              "bg": "{{ '#007A1F' if open else '#B00020' }}",
              "ttl": 0,
              "draw": [
                {"t":"text","x":120,"y":60,"s":2,"c":"#FFFFFF","a":"c",
                 "v":{{ ('Open the window' if open else 'Keep it closed') | tojson }}},
                {"t":"line","x":20,"y":110,"x2":220,"y2":110,"c":"#FFFFFF"},
                {"t":"text","x":120,"y":150,"s":2,"c":"#FFFFFF","a":"c",
                 "v":"{{ '%.1f in / %.1f out' | format(ind, out) }}"}
              ]
            }
```

Every time either sensor moves, the screen republishes; the retained message keeps the broker's copy current. The time-pattern trigger republishes within five minutes even if a state change slipped past, and the startup trigger puts the screen back after a Home Assistant restart.

The same automation exists as an importable blueprint, with the sensors, delta, hostname, slot, and labels as inputs: [`blueprints/automation/smalltv/temp_compare.yaml`](https://github.com/giovi321/smalltv-mod/blob/main/blueprints/automation/smalltv/temp_compare.yaml) in the repository. Import it under **Settings → Automations & scenes → Blueprints → Import blueprint** by pasting that URL. Ready-made YAML for all of these examples also lives in [`examples/ha/`](https://github.com/giovi321/smalltv-mod/tree/main/examples/ha/).

## More than one screen

Slots are independent, so you can build a carousel from several automations, or from one automation with several publish actions. These three publishes give you a weather line, the window advice above, and today's energy total, rotating at the device's dwell time:

```yaml
- action: mqtt.publish
  data:
    topic: smalltv/smalltv/screen/weather
    retain: true
    payload: >-
      {"bg":"#003366","draw":[
        {"t":"text","x":120,"y":80,"s":2,"c":"#FFFFFF","a":"c","v":"Outside"},
        {"t":"text","x":120,"y":130,"s":4,"c":"#FFFF00","a":"c",
         "v":"{{ states('sensor.outdoor_temperature') }} C"}]}

- action: mqtt.publish
  data:
    topic: smalltv/smalltv/screen/window
    retain: true
    payload: >-
      {"bg":"#007A1F","draw":[
        {"t":"text","x":120,"y":120,"s":2,"c":"#FFFFFF","a":"c","v":"Open the window"}]}

- action: mqtt.publish
  data:
    topic: smalltv/smalltv/screen/zenergy
    retain: true
    payload: >-
      {"bg":"#1A1A1A","draw":[
        {"t":"text","x":120,"y":80,"s":2,"c":"#FFFFFF","a":"c","v":"Today"},
        {"t":"text","x":120,"y":130,"s":3,"c":"#00FF88","a":"c",
         "v":"{{ states('sensor.energy_today') }} kWh"}]}
```

Slot order is the carousel order, so the leading `z` on `zenergy` is deliberate: it keeps that screen last. Stay under the per-board screen count: 8 slots on the ESP32 builds, 4 on the ESP8266.

## A whole board from one blueprint

Writing four near-identical automations gets old. The [`screen_board.yaml`](https://github.com/giovi321/smalltv-mod/blob/main/blueprints/automation/smalltv/screen_board.yaml) blueprint publishes up to four independent screens from a single automation, one retained message per slot. Each screen is a collapsed input section with an entity (any domain), an optional title (the entity's friendly name is used when empty), an icon from the built-in set, foreground and background colours, and a slot name. Each rendered screen is the icon centred at the top, the title below it, and the value in big type underneath, at y 30, 110, and 150 by default. Every position and size is a per-screen input, and the defaults recreate that classic layout. When a big bitmap crowds the top of the screen, move the icon up or push the title and value down to rebalance: a 24 px bitmap at scale 2 draws 48 px down from the icon's top edge. An empty entity disables that screen, unless you set its value template.

The title, value, icon, foreground, and background of each screen also take an optional Jinja template, which you edit in a template editor in the blueprint UI. A template that is set wins over its static input; one left empty, or one that renders to an empty string, falls back to the static value. With the value template set you can skip the entity entirely and treat the screen like a small Lovelace card: combine two sensors into one line, switch the icon between `window-open` and `window-closed`, turn the foreground red above a threshold.

Text always fits the panel. Each line gets a 232 px budget (a 4 px margin on each side). The title stays a single line: like the value, it takes a minimum and maximum size per screen (1 to 2 by default), uses the largest scale in that range that fits, and cuts longer text at the smallest size. The value line takes a minimum and maximum text size per screen (scales 1 to 4, the full range by default). The blueprint picks the largest size in that range that fits on one line; text that does not fit even at the smallest size wraps onto up to four lines at that size, and only the last line is cut if the text runs longer. A runaway template cannot overflow the screen or break the JSON. Other failures degrade instead of breaking the board: a template that renders a malformed colour makes the device skip that primitive, an unknown icon name draws no icon, and the rest of the screen still draws.

Templates do not add triggers. List every entity your templates use in the `watched_entities` input, or that screen only refreshes on the one-minute safety timer. Slot names sort lexicographically and must be unique per device across every automation publishing to the same hostname. Import it like the window blueprint above; a filled-in example with one templated screen lives in [`examples/ha/screen_board_instance.md`](https://github.com/giovi321/smalltv-mod/blob/main/examples/ha/screen_board_instance.md).

Each screen also takes a custom bitmap: a hex string from the MDI converter above, at 24, 32, or 48 px, pasted into a multiline field. A bitmap that is set wins over the icon template and the icon picker, and draws centred at the icon spot in the resolved foreground colour. A template variant of the field renders to a hex string and can switch bitmaps by state; when it renders non-empty it wins over the pasted bitmap, and the size picker still applies. A separate scale input (1 to 4, default 2) upscales the bitmap on the device, so a 24 px source at scale 2 draws 48 px while the payload stays small. The hex length must match the size picker (144 chars for 24 px, 256 for 32 px, 576 for 48 px) or the firmware silently skips the bitmap.

## Two sensors on one screen

The screens above each show one reading. When two readings belong together,
splitting the panel down the middle beats waiting for the carousel: 240 px
divides into two 120 px halves, centred on x 60 and x 180, and a vertical
`line` at x 120 separates them. This puts the outdoor temperature next to a
bedroom's:

```yaml
- action: mqtt.publish
  data:
    topic: smalltv/smalltv/screen/duo
    retain: true
    payload: >-
      {% set o = states('sensor.outside_temperature') | float(0) -%}
      {% set b = states('sensor.bedroom_2_temperature') | float(0) -%}
      {"bg":"#000000","ttl":0,"draw":[
      {"t":"text","x":120,"y":12,"s":2,"c":"#FFFFFF","a":"c","v":"Temperatures"},
      {"t":"line","x":120,"y":42,"x2":120,"y2":228,"c":"#444444"},
      {"t":"text","x":60,"y":84,"s":2,"c":"#8AB4F8","a":"c","v":"Outside"},
      {"t":"text","x":60,"y":118,"s":4,"c":"#8AB4F8","a":"c","v":"{{ '%.1f' | format(o) }}"},
      {"t":"text","x":60,"y":164,"s":2,"c":"#8AB4F8","a":"c","v":"°C"},
      {"t":"text","x":180,"y":84,"s":2,"c":"#FFCC00","a":"c","v":"Bedroom 2"},
      {"t":"text","x":180,"y":118,"s":4,"c":"#FFCC00","a":"c","v":"{{ '%.1f' | format(b) }}"},
      {"t":"text","x":180,"y":164,"s":2,"c":"#FFCC00","a":"c","v":"°C"}]}
```

The arithmetic that makes it fit: a column has 112 px of usable width after a
4 px margin on each side, and text is `length × 6 × scale` pixels wide. So a
column holds 4 characters at scale 4, 6 at scale 3, and 18 at scale 1. That is
why the values are rounded to one decimal and the unit sits on its own row
instead of on the value line. Feed a longer string into a scale-4 column and it
runs straight off the panel; nothing clips it for you.

Watch the payload too. The eight primitives above come to about 585 bytes,
which fits the ESP8266's ~700 B budget. Adding a per-column icon costs roughly
60 bytes each, so on an ESP8266 an icon means dropping the title or moving the
unit onto the value line. The ESP32 builds have ~2 KB and do not care. The full
automation, with triggers and an availability guard, is in
[`examples/ha/dual_sensor_two_temperatures.yaml`](https://github.com/giovi321/smalltv-mod/blob/main/examples/ha/dual_sensor_two_temperatures.yaml).

### The same screen from a blueprint

[`dual_sensor.yaml`](https://github.com/giovi321/smalltv-mod/blob/main/blueprints/automation/smalltv/dual_sensor.yaml)
publishes that layout from inputs, and sizes the text for you: each column's
label and value take a minimum and maximum size, the blueprint picks the
largest that fits 112 px, and cuts the string at the smallest size if even that
is too wide, so a sensor that suddenly reports `-1234.567` cannot overflow the
panel or break the JSON. Per column you get an entity, a label, a unit, an icon
from the built-in set, a foreground colour, a decimal cap, and a list of which
of those to draw at all; label, value, icon, and colour each also take a Jinja
template that wins over the static input when it renders non-empty. Shared
across both columns: a title, the background, the divider colour, the TTL, and
every y position and text size, in a collapsed **Layout** section.

```yaml
automation:
  - alias: SmallTV two temperatures
    use_blueprint:
      path: smalltv/dual_sensor.yaml
      input:
        hostname: smalltv
        slot: duo
        title: Temperatures
        left_entity: sensor.outside_temperature
        left_label: Outside
        left_icon: sun
        left_color: "#8AB4F8"
        right_entity: sensor.bedroom_2_temperature
        right_label: Bedroom 2
        right_icon: home
        right_color: "#FFCC00"
```

Write the labels by hand. Left empty they fall back to the entity's friendly
name, and `Bedroom 2 temperature` does not fit a 112 px column: it comes out as
`Bedroom 2 temperat`. The units are read from each sensor's
`unit_of_measurement`, so they need no input either.

Both column entities trigger the automation on their own, so `watched_entities`
is only for entities that appear inside a template and nowhere else. As with the
screen board, the screen republishes every minute to keep its TTL alive and
after every Home Assistant restart. While either sensor reads `unavailable` the
screen is not republished at all, so the panel keeps the last good pair of
readings instead of showing `unavailable` in big type.

### Turning elements off

Every part of the screen can be removed. Each column has an **Elements to
draw** multi-select holding `icon`, `label`, `value`, and `unit`; the title and
the divider are switched off by clearing their own inputs:

| Element | How to remove it | Bytes freed (both columns) |
|---|---|---|
| Title | leave **Title** empty | ~70, plus its length |
| Divider | leave **Divider colour** empty | ~60 |
| Icon | untick `icon`, or set **Icon** to `none` | ~130 |
| Label | untick `label` | ~130 |
| Unit | untick `unit` | ~135 |
| Value | untick `value` | ~135 |

The lists are per column, so a screen can be asymmetric: two temperature
columns that both read `°C` can print the unit once by unticking it on the
right. Bare numbers with no captions and no title come to about 230 bytes,
which leaves plenty of room on an ESP8266.

```yaml
# Just the two numbers and the divider, about 230 bytes
left_elements: [value]
right_elements: [value]
title: ""
```

Or icons and numbers with no captions, printing the shared unit once:

```yaml
left_elements: [icon, value, unit]
right_elements: [icon, value]
```

Nothing reflows. The remaining elements stay on the rows the **Layout** section
gives them, so removing the labels leaves a 34 px gap where they were. Close it
by moving the rest up. These starting points keep the same rhythm as the
default layout:

| Kept | Icon row | Label row | Value row | Unit row |
|---|---|---|---|---|
| icon, label, value, unit | 50 | 84 | 118 | 164 |
| label, value, unit | – | 70 | 104 | 150 |
| icon, value, unit | 56 | – | 100 | 146 |
| icon, label, value | 60 | 100 | 134 | – |
| value | – | – | 108 | – |

The arithmetic, if you want your own: `y` is an element's top pixel row, an
icon is `24 × icon scale` pixels tall, and a text row is `8 × text size`. The
value's size is chosen at render time from its **minimum** and **maximum**
inputs, so a value row is between 8 and 32 px tall at the defaults; pin the
minimum and maximum to the same number if you want a fixed height to lay out
against. With a title, the space below it runs from y 42 to y 228.

An empty element list on both columns, with no title and no divider, publishes
a valid screen with an empty draw list: the panel shows the background colour
and nothing else.

### Sensors with too many decimals

A sensor that reports `21.718751234567` is 15 characters, and 15 characters only
fit a column at text scale 1. The value is legible from across the room at scale
4 and not at scale 1, so precision the panel cannot use costs you the whole
point of the screen.

Each column has a **Decimals** input for this, defaulting to one place. It is a
cap and not a format: extra digits are rounded away, trailing zeros are dropped,
and a value that is not a number passes through untouched. So nothing ever gets
longer than the sensor reported.

| Sensor reports | Decimals | Drawn as | Text scale |
|---|---|---|---|
| `21.718751234567` | `off` | `21.718751234567` | 1 |
| `21.718751234567` | `1` | `21.7` | 4 |
| `21.718751234567` | `0` | `22` | 4 |
| `21.718751234567` | `3` | `21.719` | 3 |
| `1234` | `1` | `1234` | 4 |
| `21.0` | `1` | `21` | 4 |
| `cloudy` | `1` | `cloudy` | 3 |

The cap applies after the value template, so a template that computes a
difference is rounded too and does not need its own `round()`. Set Decimals to
`off` for a column whose value is a string you want verbatim, or where you have
already formatted it in the template.

Rounding only for display is deliberate: the sensor's own state and its history
keep the full precision, so nothing else in Home Assistant is affected.

### What the templates buy you

A few more input keys:

```yaml
# Threshold colour: red when the room is too warm, blue when too cold.
right_color_tpl: >-
  {% set t = states('sensor.bedroom_2_temperature') | float(0) %}
  {{ '#D50000' if t > 24 else '#8AB4F8' if t < 18 else '#FFCC00' }}

# Icon by state. The window contact triggers nothing on its own, so it also
# belongs in watched_entities.
left_icon_tpl: >-
  {{ 'window-open' if is_state('binary_sensor.bedroom_2_window', 'on')
     else 'window-closed' }}

# A difference rather than a reading, on a column with no entity at all. No
# round() needed: the Decimals cap runs after the value template.
right_value_tpl: >-
  {{ states('sensor.bedroom_2_temperature') | float(0)
     - states('sensor.outside_temperature') | float(0) }}
right_unit: "d °C"

# Print whatever the sensor says, for a column whose value is a string
left_decimals: "off"
```

`Δ °C` would read better than `d °C` there, but `Δ` has no glyph in the built-in
font and is dropped silently, leaving the row as ` °C`. The full character set
is listed under [Drawing on the screen](#drawing-on-the-screen).

A filled-in instance, with the threshold colour wired up, is in
[`examples/ha/dual_sensor_instance.md`](https://github.com/giovi321/smalltv-mod/blob/main/examples/ha/dual_sensor_instance.md).
Two icons plus a title bring that one to about 715 bytes, just past the
ESP8266's limit; the file lists what to turn off to get back under it.

For three or four readings, publish one screen per reading with the screen
board blueprint and let the carousel do the work. Four 60 px columns leave 52
px of usable width each, which is 8 characters at scale 1 and unreadable across
a room.

## Deleting a screen

Publish an empty retained payload to the slot's topic. The broker drops its retained copy and the device drops the screen:

```yaml
- action: mqtt.publish
  data:
    topic: smalltv/smalltv/screen/window
    retain: true
    payload: ""
```

### Screens that clean themselves up

Screens published by the screen board blueprint carry a time-to-live, set by the `screen_ttl` input (900 seconds by default). The blueprint republishes every minute, so the countdown never runs out while the automation exists. Delete the automation and its screens expire on their own within the TTL. The broker still holds the retained copy, though, so a device reboot can briefly bring a dead screen back. The remaining time is persisted across reboots, so the device expires it again on the same clock instead of restarting the countdown. Set the input to 0 for the old sticky behaviour, where a screen stays until you delete it by hand.

### Clearing everything at once

The device's web UI has a Clear screens button in the MQTT card. It wipes the device's screen store and publishes an empty retained payload for every slot the device knows, so broker zombies die at once instead of waiting out the TTL. Scripts can call the same thing as `POST /api/ha/clear`.

The button can only clear slots the device currently knows about. If the device was offline when a screen appeared or rotated out, the broker may hold a slot the device never saw. Clear that one with the `mosquitto_pub` empty-payload line below.

## Trying it without Home Assistant

Any MQTT client works. With `mosquitto_pub`, one line sets a screen and one line deletes it:

```bash
# set a screen (note -r for retain)
mosquitto_pub -h broker.local -t smalltv/smalltv/screen/hello -r -m \
  '{"bg":"#003366","draw":[{"t":"text","x":120,"y":120,"s":2,"c":"#FFFFFF","a":"c","v":"Hello"}]}'

# delete it: -n sends an empty payload
mosquitto_pub -h broker.local -t smalltv/smalltv/screen/hello -r -n

# is the panel actually connected?
mosquitto_sub -h broker.local -t smalltv/smalltv/availability -C 1 -W 2
```

If the screen does not appear, check the payload size against the limits table above first. An overlong payload is the most common cause, and a JSON syntax error is silently ignored by design.
