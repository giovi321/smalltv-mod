# Dynamic Theme Properties and Text Scrolling

## Status

Proposed design for the SmallTV Pro theme manifest and renderer. This document
extends theme spec version 1 without adding time-series storage or arbitrary
expressions.

## Motivation

Theme text can already interpolate values fetched from small JSON endpoints, but
the resulting text is clipped by the display when it is too long. Fetched values
also cannot affect geometry or style, which prevents authors from building
instantaneous bars, gauges, indicators, and threshold-colored status panels.

This change adds two related capabilities:

1. horizontally scrolling text with configurable loop and bounce behavior;
2. safe declarative bindings from fetched scalar values to selected numeric and
   color properties.

The format must remain deterministic, bounded, and directly representable by a
future visual editor. It must not become a scripting or expression language.

## Goals

- Scroll a text layer only when its rendered text is wider than a declared
  viewport.
- Support continuous loop and back-and-forth bounce modes.
- Let a fetched number control supported positions and dimensions through a
  clamped linear mapping.
- Let a fetched number select text, fill, or stroke colors from ordered stops.
- Support rounded rectangles with a static or data-bound corner radius.
- Recompute bindings when fetched values change without mutating the parsed
  theme definition.
- Repaint only the union of the old and new visible regions.
- Give an editor a closed schema with explicit property names, types, ranges,
  and applicability rules.
- Preserve current behavior for every existing manifest.

## Non-goals

- Historical samples or line charts.
- Arrays of samples, aggregation, arithmetic expressions, conditions, or
  JavaScript-like formulas.
- Vertical or diagonal text scrolling.
- Runtime font loading, wrapping, or multiline text.
- Animated transitions for ordinary numeric bindings.
- Bindings to asset paths, text templates, data-source URLs, animation frame
  counts, or other resource-bearing properties.

## Manifest compatibility

The additions are optional fields within theme spec version 1. Existing themes
remain valid and render exactly as before. Unknown fields remain errors.

The complete new fields on a text layer are:

```json
{
  "scroll": {
    "width": 200,
    "mode": "loop",
    "speed": 30,
    "pause": 1000,
    "gap": 24
  },
  "bind": {}
}
```

All layer types may optionally contain `bind`, subject to the property matrix
below.

## Text scrolling

### Schema

`scroll` is allowed only on a text layer and is an object with these fields:

| Field | Type | Range | Required | Meaning |
| --- | --- | --- | --- | --- |
| `width` | integer | 1–240 px | yes | Width of the clipping viewport |
| `mode` | string | `loop`, `bounce` | yes | Motion behavior |
| `speed` | integer | 1–240 px/s | yes | Horizontal speed |
| `pause` | integer | 0–10000 ms | no, default 1000 | Hold time at the initial position and, for bounce, at both ends |
| `gap` | integer | 0–240 px | loop only, default 24 | Blank space between repeated copies |

`gap` is rejected in bounce mode because it has no effect. Unknown scroll fields,
wrong types, and out-of-range values invalidate the manifest.

### Viewport placement

The text layer's `x`, `y`, and `anchor` position the viewport when `scroll` is
present. The viewport height is the text `size`; its width is `scroll.width`.
For example, `x: 120` with `anchor: center` produces a viewport centered at
120 rather than centering the complete unscrolled string.

The viewport is clipped to the 240×240 canvas like every other layer. Glyphs are
additionally clipped to the viewport even when another dirty region is wider.

### Activation

Let `textWidth` be the existing bitmap-cell width calculation and
`viewportWidth` be the resolved scroll width.

- If `textWidth <= viewportWidth`, the text remains fixed at its initial
  position. No scroll timer causes display writes.
- If `textWidth > viewportWidth`, scrolling starts after `pause` milliseconds.
- Changing the expanded text or a binding that affects text size or scroll
  geometry resets the scroll to its initial position and restarts the initial
  pause.
- Invalidating the engine for notification recovery does not reset scroll
  progress; switching themes does.

### Loop mode

The content moves from right to left. The renderer draws a second copy after
`textWidth + gap` so the transition is continuous. Once the first copy has moved
by that distance, the offset wraps to zero and the initial pause applies again.

### Bounce mode

The content starts at offset zero, moves left until its right edge meets the
viewport's right edge, pauses, moves right back to zero, pauses, and repeats.
The offset never exposes blank space beyond the natural leading or trailing
edge.

### Timing

Scrolling uses elapsed milliseconds and a subpixel phase accumulator, as frame
animation already does. Late updates jump directly to the due integer pixel
offset; they never enqueue intermediate frames. `millis()` wraparound is handled
with unsigned subtraction.

The engine reports a dirty region only when the visible integer pixel offset
changes. The dirty region is exactly the clipped viewport.

## Dynamic bindings

### Binding object

`bind` is an object keyed by a supported property name. Each property contains
exactly one numeric mapping or color-stop binding. A source is the same declared
`<data-source-id>.<field-id>` token accepted by text interpolation.

Numeric example:

```json
{
  "bind": {
    "width": {
      "source": "weather.temp",
      "input": [0, 40],
      "output": [0, 200],
      "clamp": true
    }
  }
}
```

A rounded rectangle declares `cornerRadius` directly on the shape:

```json
{
  "id": "temperature-bar",
  "type": "shape",
  "shape": "rectangle",
  "x": 20,
  "y": 120,
  "width": 200,
  "height": 18,
  "cornerRadius": 6,
  "fill": "#35c46a"
}
```

`cornerRadius` is optional and defaults to zero, preserving square corners. Its
static manifest range is 0–120 pixels. After all numeric bindings are resolved,
the effective value is clamped to `floor(min(width, height) / 2)`. A rectangle
whose dynamic width or height is zero remains invisible regardless of radius.

Color example:

```json
{
  "bind": {
    "fill": {
      "source": "weather.temp",
      "stops": [
        {"at": 0, "value": "#35c46a"},
        {"at": 25, "value": "#f0b429"},
        {"at": 35, "value": "#e5484d"}
      ]
    }
  }
}
```

### Numeric mapping schema

| Field | Type | Required | Rule |
| --- | --- | --- | --- |
| `source` | string | yes | Must reference a declared data field |
| `input` | two finite numbers | yes | Endpoints must differ; ascending or descending is allowed |
| `output` | two integers | yes | Both must fit the target property's runtime range below |
| `clamp` | boolean | no, default true | Clamp to output endpoints when true |

The mapping is linear:

```text
output0 + (source - input0) * (output1 - output0) / (input1 - input0)
```

The result is rounded to the nearest integer, with halves away from zero. When
`clamp` is true, the normalized input is limited to the interval between the two
input endpoints before interpolation. When false, extrapolation is permitted,
but the final integer must still be clamped to the target property's global safe
range. This last clamp is mandatory and cannot be disabled.

Runtime numeric ranges are:

| Property | Range |
| --- | --- |
| `x`, `y`, `x2`, `y2` | −240 through 479 |
| `width`, `height`, `radius` | 0–240 |
| `cornerRadius` | 0–120, then limited to half the resolved rectangle size |
| `size` | 8–96 |
| `strokeWidth` | 1–32 |
| `scroll.width` | 1–240 |
| `scroll.speed` | 1–240 |

Dynamic shape dimensions may resolve to zero even though their static manifest
fallback must be at least one pixel. Zero makes the shape temporarily invisible,
which is required for bars whose source is at the bottom of its input range.

### Color-stop schema

| Field | Type | Required | Rule |
| --- | --- | --- | --- |
| `source` | string | yes | Must reference a declared data field |
| `stops` | array | yes | 1–8 entries |
| `stops[].at` | finite number | yes | Strictly increasing |
| `stops[].value` | `#RRGGBB` | yes | Existing color syntax |

The selected color is the last stop whose `at` is less than or equal to the
source value. Values below the first stop use the first stop's color. This gives
the binding a total result for every finite number and avoids a separate default
branch.

### Supported properties

| Layer | Numeric properties | Color properties |
| --- | --- | --- |
| text | `x`, `y`, `size`, `scroll.width`, `scroll.speed` | `color` |
| rectangle | `x`, `y`, `width`, `height`, `cornerRadius`, `strokeWidth` | `fill`, `stroke` |
| circle | `x`, `y`, `radius`, `strokeWidth` | `fill`, `stroke` |
| line | `x`, `y`, `x2`, `y2`, `strokeWidth` | `stroke` |
| image | `x`, `y` | none |
| animation | `x`, `y` | none |

Image and animation dimensions are not bindable in this version because runtime
scaling is unsupported and their declared dimensions must match packaged assets.

A color property may be bound only when that property exists statically on the
layer. For example, binding `stroke` does not implicitly enable a missing stroke.
Likewise, `scroll.width` and `scroll.speed` require a static `scroll` object.

Each property may be bound once. A layer may bind at most eight properties. A
manifest containing an inapplicable property, duplicated property, unknown
source, mixed numeric/color fields, or excessive binding count is rejected.

## Runtime value handling

Fetched values remain stored as strings for text interpolation. Binding
resolution parses a complete string as a finite decimal number. Leading and
trailing ASCII whitespace may be ignored; trailing units, partial parses,
`NaN`, and infinity are invalid.

For a missing or invalid numeric value, the property uses its static manifest
value. This is also the state before the first successful fetch. A failed refresh
continues to expose the last successful source values under the existing data
client policy.

Bindings are recalculated only when `setValues` receives a changed value set or
when a theme is installed. No network logic enters the renderer.

## Engine model

The parsed `Theme` and its `Layer` definitions remain immutable after
`Engine::setTheme`. Each `LayerState` gains resolved runtime properties required
by its layer type, plus scroll timing and offset state for text.

Resolution follows this order:

1. copy static layer properties into the resolved state;
2. apply valid numeric bindings;
3. apply valid color bindings;
4. expand text variables;
5. compute text width, viewport, and visible bounds;
6. update scroll scheduling.

Keeping resolved state separate prevents data updates from corrupting manifest
fallbacks and lets authoring tools inspect the original declaration.

## Bounds, invalidation, and rendering

When a data update changes geometry or style, the dirty region is the union of
the old and new visible bounds. A pure color change invalidates the current
bounds. Layers above the changed layer are recomposited through the existing
back-to-front row renderer, preserving overlays.

Rounded rectangle fills include pixels inside the four quarter-circle corners.
Their stroke follows the inside edge of the same rounded outline, consistent
with the existing rule that rectangle strokes do not grow the layer bounds.
`cornerRadius: 0` uses the existing square-rectangle rendering path.

For scrolling text, `LayerState::bounds` is the viewport rather than the complete
string. Rendering maps each destination pixel through the scroll offset to the
appropriate glyph cell. Loop mode may map into either of two repeated copies;
gap pixels remain transparent so lower layers show through.

Resolved coordinates and bounds continue to be clipped at render time. Numeric
bindings cannot allocate buffers or access assets based on fetched content.

## Tooling and editor contract

The native parser remains the source of truth. The Python packer and browser
preview must accept and render the same fields and reject the same invalid
combinations.

An editor can derive its controls from the closed property matrix:

- show scroll controls only for text;
- show `gap` only for loop mode;
- offer binding targets only when valid for the selected layer;
- offer only declared data fields as binding sources;
- use numeric range editors for `input` and `output`;
- use an ordered stop editor for colors;
- preserve static values as the visible fallback configuration.

This document deliberately avoids editor-specific layout, state management, or
serialization APIs. The editor should emit the manifest shape defined here and
treat device/native-parser validation errors as authoritative.

## Validation and error reporting

Parser diagnostics must identify the complete rejected field path, for example:

```text
layers[3].scroll.mode: expected loop or bounce
layers[2].bind.width.input: endpoints must differ
layers[4].bind.fill.stops[1].at: stops must be strictly increasing
```

Installing a package with any invalid scroll or binding declaration fails before
the package is published.

## Testing

Automated coverage must include:

- parser acceptance and rejection for every new field, range, layer/property
  combination, source reference, and stop-order rule;
- fixed text when content fits the viewport;
- loop scheduling, wrap, gap rendering, initial pause, late updates, and
  `millis()` wraparound;
- bounce direction changes and endpoint pauses;
- reset on changed text, size, or scroll geometry;
- no reset on unrelated data changes or notification invalidation;
- ascending, descending, clamped, and extrapolated numeric mappings;
- rounding and final safe-range clamping;
- square and rounded rectangle fill/stroke rendering, including a dynamically
  resized rectangle whose effective corner radius must shrink;
- invalid/missing numeric source fallback;
- threshold selection below, at, between, and above stops;
- dirty-region unions for moving/resizing layers and current-bounds invalidation
  for color changes;
- layer compositing above and below scrolling or dynamically resized content;
- packer/native validation parity and browser-preview parity;
- deterministic rebuilds of examples;
- a clean `smalltv_esp32_8mb` firmware build and flash-size report.

At least one bundled example theme must use fetched data to drive scrolling text,
a numeric bar, and threshold colors. Tests may use deterministic injected values;
the example must not depend on a public service during CI.

## Documentation changes

The public theme documentation must add:

- the complete scroll schema and both modes;
- viewport/anchor semantics;
- numeric mapping and color-stop schemas;
- the supported-property matrix;
- fallback, parsing, rounding, clamping, and invalidation behavior;
- an end-to-end JSON-data dashboard example;
- limits and performance implications.

The pull request description should summarize the new manifest surface and note
that history/line charts remain out of scope.
