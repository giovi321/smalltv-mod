# Dynamic Theme Properties and Text Scrolling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable loop/bounce text scrolling, fetched-data-driven numeric and color properties, and rounded rectangles to the SmallTV Pro theme engine and authoring workflow.

**Architecture:** Extend the strict spec-v1 parser with bounded scroll and binding declarations, then resolve those immutable declarations into per-layer runtime state whenever data changes. Keep scheduling and dirty-region calculation in `Engine`, keep scanline composition in `render`, and reuse the native firmware implementation for package validation and previews so desktop tooling cannot drift from device behavior.

**Tech Stack:** C++11, ArduinoJson 7, Arduino/ESP32, LittleFS, Python 3/Pillow, Node.js browser checks, PlatformIO.

**Spec:** `docs/superpowers/specs/2026-09-22-dynamic-theme-properties-design.md`

## Global Constraints

- Theme format remains spec version 1; every new field is optional and old packages must render unchanged.
- No history, line charts, arrays, formulas, arbitrary expressions, runtime fonts, wrapping, or multiline text.
- Scroll limits: width 1–240 px, speed 1–240 px/s, pause 0–10000 ms, gap 0–240 px.
- Numeric runtime limits: coordinates −240..479; shape width/height/radius 0..240; corner radius 0..120 then clamped to half the resolved rectangle; text size 8..96; stroke width 1..32.
- A layer may bind at most eight properties; a color binding may contain at most eight strictly increasing stops.
- Missing, partial, non-finite, or otherwise invalid fetched numbers use the static manifest property.
- The parsed `Theme` remains immutable after `Engine::setTheme`; fetched values affect only resolved `LayerState`.
- Rendering remains scanline-based with fixed 240-pixel buffers; no full framebuffer or fetched-data-sized allocation.
- Image and animation dimensions remain static because runtime asset scaling is unsupported.
- The native parser is authoritative for packer and preview validation.

## Review Focus

- A fetched value at, below, and above reversed input endpoints must map deterministically, with safe-range clamping even when `clamp:false`; Task 2 pins all cases.
- A text/data update arriving during a loop or bounce endpoint pause must restart at offset zero without inheriting stale phase or direction; Task 3 pins this transition.
- A dynamic rectangle shrinking below twice its declared/bound `cornerRadius` must clamp its radius without drawing outside its resolved bounds; Task 4 pins fill and stroke pixels.
- A dirty region wider than a scroll viewport must never reveal scrolling glyphs outside the viewport; Task 4 renders overlapping foreground/background layers to pin clipping.
- Missing or non-finite bound data must restore static geometry/color and erase the previously resolved dynamic region; Tasks 2 and 4 pin fallback plus old/new dirty unions.

---

## File Structure

- Modify `src/features/theme/ThemeEngine.h`: declaration model (`Scroll`, numeric/color bindings), resolved `LayerState`, and pure resolver interfaces.
- Modify `src/features/theme/ThemeEngine.cpp`: strict parsing, scalar conversion, mapping/color resolution, scroll scheduling, bounds, rounded-rectangle hit testing, and scanline rendering.
- Modify `tests/theme/test_validation.cpp`: exhaustive schema acceptance/rejection and diagnostic paths.
- Modify `tests/theme/test_engine.cpp`: pure binding resolution, scroll timing, fallback, and dirty-region behavior.
- Modify `tests/theme/test_render.cpp`: pixel-level scrolling, clipping, compositing, and rounded-rectangle rendering.
- Modify `tests/theme/test_data.cpp`: asynchronous fetched-value changes reset or preserve scroll state correctly.
- Modify `tools/theme_native.cpp`: deterministic injected preview values for dynamic-theme authoring.
- Modify `tools/smalltv_theme.py`: repeatable `--data key=value` preview arguments.
- Modify `tools/theme_preview.py`: forward injected values to the native renderer and record them in preview metadata.
- Modify `tests/theme/test_cli.py`: dynamic preview and invalid injection tests.
- Modify `tests/theme/test_preview_browser.mjs`: confirm dynamic preview remains offline and playable.
- Create `examples/themes/live-status/theme.json`: documented data-driven dashboard source.
- Create `examples/themes/live-status-preview.html`: deterministic generated preview.
- Create `examples/themes/live-status.stheme`: reproducible package.
- Modify `examples/themes/README.md`: describe the third example and preview values.
- Modify `docs/src/content/docs/features/themes.md`: public schema, property matrix, examples, limits, and diagnostics.
- Modify `.github/workflows/build.yml`: rebuild/compare and validate the new example.
- Modify the existing PR description after implementation and verification.

### Task 1: Parse Scroll, Bindings, and Rounded Rectangles

**Files:**
- Modify: `src/features/theme/ThemeEngine.h`
- Modify: `src/features/theme/ThemeEngine.cpp`
- Modify: `tests/theme/test_validation.cpp`

**Interfaces:**
- Consumes: existing `Fields`, `parseColor`, `validText`, declared `ThemeDataSource` fields.
- Produces: `ScrollMode`, `Scroll`, `NumericBinding`, `ColorStop`, `ColorBinding`, `Binding`, `Layer::scroll`, `Layer::bindings`, and `Layer::cornerRadius` for later engine tasks.

- [ ] **Step 1: Write failing acceptance tests for the new manifest fields**

Add a helper to `tests/theme/test_validation.cpp` that starts from `dataBase`, then assert a complete declaration parses into the expected model:

```cpp
deserializeJson(d,dataBase);
JsonObject layer=d["layers"][0];
layer["scroll"]["width"]=120;
layer["scroll"]["mode"]="bounce";
layer["scroll"]["speed"]=24;
layer["scroll"]["pause"]=750;
layer["bind"]["x"]["source"]="weather.temp";
layer["bind"]["x"]["input"].add(0.0);
layer["bind"]["x"]["input"].add(40.0);
layer["bind"]["x"]["output"].add(0);
layer["bind"]["x"]["output"].add(200);
layer["bind"]["color"]["source"]="weather.temp";
JsonObject cold=layer["bind"]["color"]["stops"].add<JsonObject>();
cold["at"]=0;cold["value"]="#00ff00";
JsonObject hot=layer["bind"]["color"]["stops"].add<JsonObject>();
hot["at"]=30;hot["value"]="#ff0000";
serializeJson(d,json);
assert(parseTheme(json,theme,error));
assert(theme.layers[0].scroll.enabled);
assert(theme.layers[0].scroll.mode==ScrollMode::Bounce);
assert(theme.layers[0].bindings.size()==2);
```

Add a rectangle case with `cornerRadius: 8` and a `cornerRadius` numeric binding.

- [ ] **Step 2: Write failing rejection tests for every closed-schema rule**

Use the existing `rejected(JsonDocument&)` helper to cover:

```cpp
// scroll: text only, required fields, type/range, mode/gap compatibility
// bind: object only, <=8 entries, unique known target, declared source
// numeric: exact keys, two finite input/output values, input endpoints differ
// color: exact keys, 1..8 stops, finite and strictly increasing at values
// applicability: no image width binding, no absent fill/stroke binding,
//                no scroll.width without a scroll object
// cornerRadius: rectangle only, integer 0..120
```

For representative cases, assert precise prefixes such as:

```cpp
assert(error.find("layers[0].scroll.mode:")==0);
assert(error.find("layers[0].bind.width.input:")==0);
assert(error.find("layers[0].bind.fill.stops[1].at:")==0);
```

- [ ] **Step 3: Run the validation test and verify RED**

Run:

```bash
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
  tests/theme/test_validation.cpp src/features/theme/ThemeEngine.cpp \
  -o /tmp/smalltv-theme-validation-tests
/tmp/smalltv-theme-validation-tests
```

Expected: compilation fails because the new model types/fields do not exist, or the first new parse assertion fails.

- [ ] **Step 4: Add the bounded declaration model**

Add these types to `ThemeEngine.h` (use the exact names throughout later tasks):

```cpp
enum class ScrollMode { Loop, Bounce };
struct Scroll {
  bool enabled=false;
  int width=0, speed=0, pauseMs=1000, gap=24;
  ScrollMode mode=ScrollMode::Loop;
};
enum class BoundProperty {
  X,Y,Width,Height,Radius,CornerRadius,X2,Y2,Size,StrokeWidth,
  ScrollWidth,ScrollSpeed,Color,Fill,Stroke
};
struct NumericBinding {
  double input0=0,input1=1;
  int output0=0,output1=1;
  bool clamp=true;
};
struct ColorStop { double at=0; uint16_t value=0; };
struct ColorBinding { std::vector<ColorStop> stops; };
struct Binding {
  BoundProperty property=BoundProperty::X;
  std::string source;
  bool color=false;
  NumericBinding numeric;
  ColorBinding colors;
};
```

Extend `Layer` with `int cornerRadius=0; Scroll scroll; std::vector<Binding> bindings;`.

- [ ] **Step 5: Implement strict nested parsing and applicability checks**

In `ThemeEngine.cpp`, add `Fields::real`, fixed-length numeric-array parsing, declared-source lookup, property-name mapping, and per-layer applicability. Keep diagnostics rooted at the exact nested path. Parse `cornerRadius` in the rectangle allowed-key list and default it to zero.

Use finite checks explicitly:

```cpp
bool Fields::real(const char* key,double& value) const {
  JsonVariantConst v=object_[key];
  if(!v.is<double>()&&!v.is<long>()&&!v.is<int>())
    return fail(key,"expected a finite number");
  value=v.as<double>();
  return std::isfinite(value)||fail(key,"expected a finite number");
}
```

Reject a binding before pushing it unless its `source` exactly matches a declared `<source>.<field>` key.

- [ ] **Step 6: Run validation tests and the existing parser suite**

Run the command from Step 3, then:

```bash
sh tests/theme/run.sh
```

Expected: all theme tests pass; existing manifests still validate unchanged.

- [ ] **Step 7: Commit the parser slice**

```bash
git add src/features/theme/ThemeEngine.h src/features/theme/ThemeEngine.cpp tests/theme/test_validation.cpp
git commit -m "feat(theme): parse scrolling and dynamic bindings"
```

### Task 2: Resolve Numeric and Color Bindings

**Files:**
- Modify: `src/features/theme/ThemeEngine.h`
- Modify: `src/features/theme/ThemeEngine.cpp`
- Modify: `tests/theme/test_engine.cpp`

**Interfaces:**
- Consumes: `Layer::bindings`, `BoundProperty`, `ThemeValue` from Task 1.
- Produces: `ResolvedLayer`, `parseFiniteNumber`, `resolveNumericBinding`, `resolveColorBinding`, and `Engine::resolveLayer(size_t)` used by scheduling/rendering.

- [ ] **Step 1: Write failing pure resolver tests**

Add direct tests in `test_engine.cpp` for these exact public helpers:

```cpp
double number=0;
assert(parseFiniteNumber(" 20.5 ",number)&&number==20.5);
for(const char* bad:{"","20C","nan","inf","1e999"})
  assert(!parseFiniteNumber(bad,number));

NumericBinding ascending;
ascending.input0=0;ascending.input1=40;
ascending.output0=0;ascending.output1=200;ascending.clamp=true;
assert(resolveNumericBinding(ascending,20,-240,479)==100);
assert(resolveNumericBinding(ascending,-5,-240,479)==0);

NumericBinding reversed=ascending;
reversed.input0=40;reversed.input1=0;
assert(resolveNumericBinding(reversed,30,-240,479)==50);

NumericBinding extrapolated=ascending;extrapolated.clamp=false;
assert(resolveNumericBinding(extrapolated,100,0,240)==240);

ColorBinding colors;
ColorStop green;green.at=0;green.color=0x07e0;colors.stops.push_back(green);
ColorStop amber;amber.at=25;amber.color=0xfd20;colors.stops.push_back(amber);
ColorStop red;red.at=35;red.color=0xf800;colors.stops.push_back(red);
assert(resolveColorBinding(colors,-1)==0x07e0);
assert(resolveColorBinding(colors,25)==0xfd20);
assert(resolveColorBinding(colors,100)==0xf800);
```

Use explicit member assignment if aggregate initialization is rejected by C++11 default member initializers.

- [ ] **Step 2: Write failing engine-state tests for fallback and dirty unions**

Build a rectangle with static `x=20,width=10,fill=green`, bind width to
`sensor.value`, and bind fill to two stops. Assert:

```cpp
e.setTheme(theme,0);
e.update(0,&time);
e.setValues({{"sensor.value","50"}});
auto dirty=e.update(1,&time);
assert(e.states()[0].resolved.width==100);
assert(e.states()[0].resolved.fill==warningColor);
assert(dirty.size()==1);
assert(dirty[0].x==20 && dirty[0].y==120 &&
       dirty[0].w==100 && dirty[0].h==18);

e.setValues({{"sensor.value","not-a-number"}});
dirty=e.update(2,&time);
assert(e.states()[0].resolved.width==10);
assert(e.states()[0].resolved.fill==staticGreen);
assert(dirty.size()==1);
assert(dirty[0].x==20 && dirty[0].y==120 &&
       dirty[0].w==100 && dirty[0].h==18); // erases the old dynamic extent
```

Also assert an unrelated changed value does not dirty the bound layer.

- [ ] **Step 3: Run `test_engine` and verify RED**

Run:

```bash
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
  tests/theme/test_engine.cpp src/features/theme/ThemeEngine.cpp \
  -o /tmp/smalltv-theme-tests
/tmp/smalltv-theme-tests
```

Expected: compilation fails on missing resolver interfaces.

- [ ] **Step 4: Add resolved layer state and pure resolvers**

Define in `ThemeEngine.h`:

```cpp
struct ResolvedLayer {
  int x=0,y=0,width=0,height=0,x2=0,y2=0,radius=0,cornerRadius=0;
  int size=16,strokeWidth=1,scrollWidth=0,scrollSpeed=0;
  uint16_t color=0xffff,fill=0,stroke=0;
};
bool parseFiniteNumber(const std::string&,double&);
int resolveNumericBinding(const NumericBinding&,double,int lo,int hi);
uint16_t resolveColorBinding(const ColorBinding&,double);
```

Add `ResolvedLayer resolved;` to `LayerState`.

Implement decimal parsing with `strtod`, full-consumption checks after ASCII
whitespace, `errno != ERANGE`, and `std::isfinite`. Implement interpolation in
`double`, use `std::round`, then safe-range clamp. Use the first color below all
stops and the last `at <= value` otherwise.

- [ ] **Step 5: Resolve static values plus bindings without mutating `Layer`**

Add a private `Engine::resolveLayer(size_t index)` that copies static values,
looks up each binding source in `values_`, falls back if parsing fails, and clamps
resolved rectangle `cornerRadius` after width/height resolution:

```cpp
state.resolved.cornerRadius=std::min(state.resolved.cornerRadius,
  std::max(0,std::min(state.resolved.width,state.resolved.height)/2));
```

Track whether resolved geometry or color changed so `update` can dirty the union
of old/new bounds or the current bounds for color-only changes.

- [ ] **Step 6: Run focused and full engine tests**

Run the Step 3 command and `sh tests/theme/run.sh`.

Expected: all pass, including missing/non-finite fallback and reversed-range cases.

- [ ] **Step 7: Commit the resolver slice**

```bash
git add src/features/theme/ThemeEngine.h src/features/theme/ThemeEngine.cpp tests/theme/test_engine.cpp
git commit -m "feat(theme): resolve data-bound layer properties"
```

### Task 3: Schedule Loop and Bounce Text Scrolling

**Files:**
- Modify: `src/features/theme/ThemeEngine.h`
- Modify: `src/features/theme/ThemeEngine.cpp`
- Modify: `tests/theme/test_engine.cpp`
- Modify: `tests/theme/test_data.cpp`

**Interfaces:**
- Consumes: resolved text `size`, `scrollWidth`, `scrollSpeed` from Task 2.
- Produces: scroll fields on `LayerState` (`scrollOffset`, `scrollPhase`, `scrollLastMs`, `scrollPauseUntil`, `scrollDirection`) and viewport bounds consumed by Task 4.

- [ ] **Step 1: Write failing loop timing tests**

Create a 10-character, size-8 text layer (60 px), viewport 24 px, speed 20 px/s,
pause 1000 ms, gap 6. Assert:

```cpp
e.setTheme(theme,0);e.update(0,&time);
assert(e.states()[0].bounds.w==24);
assert(e.update(999,&time).empty());
assert(e.update(1050,&time).size()==1);
assert(e.states()[0].scrollOffset==1);
// A late update lands directly on its due pixel.
e.update(1500,&time);assert(e.states()[0].scrollOffset==10);
// textWidth + gap wraps to zero and re-enters pause.
e.update(4300,&time);assert(e.states()[0].scrollOffset==0);
```

Add `millis()` wraparound coverage by setting the theme near `0xffffffc0`.

- [ ] **Step 2: Write failing bounce and reset tests**

Assert offset progression to `textWidth - viewportWidth`, pause at the far edge,
direction reversal, return to zero, and pause there. Then change the bound text
value and assert offset/phase/direction reset. Change an unrelated data value and
assert they do not reset. Call `invalidate()` and assert it produces a full dirty
region without resetting scroll progress.

In `test_data.cpp`, complete one asynchronous `DataFetch` with a longer text value
and assert the next engine update resets scrolling exactly once.

- [ ] **Step 3: Run engine/data tests and verify RED**

Run `sh tests/theme/run.sh`.

Expected: new scroll-state assertions fail because the state and scheduling are absent.

- [ ] **Step 4: Implement viewport bounds and scroll state reset**

Change text bounds so a scroll-enabled text layer uses resolved `scrollWidth` and
the existing anchor calculation. Store expanded full text separately from its
viewport. Reset scroll state only when expanded text, resolved size, resolved
scroll width, or resolved scroll speed changes.

Initialize/reset with:

```cpp
s.scrollOffset=0;s.scrollPhase=0;s.scrollDirection=-1;
s.scrollLastMs=now;s.scrollPauseUntil=now+l.scroll.pauseMs;
```

Use wrap-safe deadline checks based on signed difference or elapsed unsigned
durations; do not compare raw `millis()` with `>` across wrap.

- [ ] **Step 5: Implement loop and bounce advancement**

Extract `advanceScroll(const Layer&,LayerState&,uint32_t now,int textWidth)`.
Accumulate `elapsed * speed + phase`, advance only whole pixels, and calculate
the final due offset/direction when an update crosses endpoints or entire cycles.
Do not iterate once per elapsed pixel; reduce loop mode modulo `textWidth + gap`
and bounce mode by its two-leg cycle.

- [ ] **Step 6: Run focused and full tests**

Run `sh tests/theme/run.sh`; the test itself must execute both ordinary and
near-`UINT32_MAX` start times in the same deterministic process.

Expected: deterministic passes with no display dirty event when the integer offset is unchanged.

- [ ] **Step 7: Commit the scheduler slice**

```bash
git add src/features/theme/ThemeEngine.h src/features/theme/ThemeEngine.cpp \
  tests/theme/test_engine.cpp tests/theme/test_data.cpp
git commit -m "feat(theme): schedule loop and bounce text scrolling"
```

### Task 4: Render Scrolling Text and Rounded Rectangles

**Files:**
- Modify: `src/features/theme/ThemeEngine.cpp`
- Modify: `tests/theme/test_render.cpp`

**Interfaces:**
- Consumes: `ResolvedLayer`, scroll viewport, offset, direction, and full expanded text from Tasks 2–3.
- Produces: pixel-correct scanline composition for scrolling text and square/rounded rectangles.

- [ ] **Step 1: Write failing scrolling pixel tests**

Use the existing `Pixels` display with distinguishable glyph columns. Cover:

```cpp
// text wider than viewport: no ink may appear outside the 24-pixel viewport
// loop: offset selects the expected glyph and gap pixels reveal background
// loop: second copy appears after textWidth + gap
// bounce: far-edge offset shows the trailing glyph without blank overshoot
// a wider dirty region from an overlapping layer still clips text to viewport
// foreground overlay is recomposited unchanged above moving text
```

Assert exact RGB565 pixels immediately before, inside, and after the viewport.

- [ ] **Step 2: Write failing rounded-rectangle pixel tests**

Test a 10×8 rectangle with `cornerRadius=3`, fill plus two-pixel stroke:

```cpp
assert(backgroundAt(0,0));       // clipped corner
assert(strokeAt(3,0));           // rounded outer edge
assert(fillAt(3,3));             // interior
assert(backgroundOutsideBounds());
```

Then bind width down to 4 and assert the effective radius becomes 2, with no ink
outside the 4-pixel resolved width. Retain the existing square-path assertions for
`cornerRadius=0`.

- [ ] **Step 3: Run render tests and verify RED**

Run:

```bash
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
  tests/theme/test_render.cpp src/features/theme/ThemeEngine.cpp \
  -o /tmp/smalltv-theme-render-tests
/tmp/smalltv-theme-render-tests
```

Expected: scrolling/rounded pixel assertions fail.

- [ ] **Step 4: Render text through viewport-local source coordinates**

For each destination pixel in the viewport, compute the content coordinate from
`scrollOffset`. In loop mode, reduce it modulo `textWidth + gap`; skip transparent
gap pixels. In bounce mode, use `scrollOffset + viewportLocalX`. Convert content
coordinate to glyph cell/column using resolved size, never static `Layer::size`.

- [ ] **Step 5: Add rounded rectangle hit testing**

Extract a pure helper that classifies a pixel against the resolved rounded outer
and inner rectangles. Use squared-distance comparisons in corner quadrants and
64-bit intermediates. The inner radius is `max(0,cornerRadius-strokeWidth)`; fill
occupies the inner shape and stroke occupies outer-minus-inner. Keep the current
fast square checks when `cornerRadius==0`.

- [ ] **Step 6: Run render and full theme suites**

Run the Step 3 command and `sh tests/theme/run.sh`.

Expected: all render assertions and existing image/animation composition tests pass.

- [ ] **Step 7: Commit the renderer slice**

```bash
git add src/features/theme/ThemeEngine.cpp tests/theme/test_render.cpp
git commit -m "feat(theme): render scrolling text and rounded rectangles"
```

### Task 5: Add Deterministic Dynamic Values to Native Previews

**Files:**
- Modify: `tools/theme_native.cpp`
- Modify: `tools/smalltv_theme.py`
- Modify: `tools/theme_preview.py`
- Modify: `tests/theme/test_cli.py`
- Modify: `tests/theme/test_preview_browser.mjs`

**Interfaces:**
- Consumes: existing `Engine::setValues(std::vector<ThemeValue>)`.
- Produces: repeatable CLI option `--data source.field=value`, forwarded to the native preview command as trailing arguments.

- [ ] **Step 1: Write failing CLI tests for injected values**

Create a temporary theme with scrolling text and a bound bar. Invoke:

```python
result = self.run_cli(
    'preview', source, source/'dynamic.html', '--seconds', 2, '--fps', 10,
    '--time', '2026-09-18T10:00:00',
    '--data', 'sensor.label=ABCDEFGHIJKLMNOPQRSTUVWXYZ',
    '--data', 'sensor.level=75')
self.assertEqual(result.returncode, 0, result.stderr)
data = self.preview_data(source/'dynamic.html')
self.assertGreater(len(set(data['frames'])), 1)  # scroll moves
self.assertEqual(data['values']['sensor.level'], '75')
```

Add failures for a key not declared by the theme, missing `=`, an empty key, a
duplicate key, and more than 32 injected values. Confirm no traceback leaks.

- [ ] **Step 2: Run the CLI test and verify RED**

Run:

```bash
python3 -m unittest tests.theme.test_cli.ThemeCliTests -v
```

Expected: argparse rejects `--data` as unknown.

- [ ] **Step 3: Add repeatable `--data` parsing**

In `smalltv_theme.py`:

```python
preview.add_argument('--data', action='append', default=[], metavar='KEY=VALUE',
                     help='Inject a declared data value into every preview frame')
```

Parse once into an insertion-ordered dict, reject duplicates/invalid keys/count,
and pass it to `write_preview(..., values=values)`.

- [ ] **Step 4: Forward values to the native renderer**

Change `write_preview` to accept `values=None`, append `key=value` arguments to
`run('preview', ...)`, and include the dict under `values` in embedded preview
metadata. In `theme_native.cpp`, accept trailing `key=value` arguments after
`FRAMES`, validate each key against declared fields, build `ThemeValue` entries,
and call `engine.setValues(values)` before the first frame.

Do not permit injected data to alter package validation or asset access.

- [ ] **Step 5: Extend the browser test**

Open the generated dynamic preview and assert playback advances, no runtime
exceptions occur, and all requests remain `file:` or `data:`. The preview remains
a recorded offline artifact; it does not fetch the manifest's URL in a browser.

- [ ] **Step 6: Run CLI and optional real-browser tests**

Run:

```bash
python3 -m unittest tests.theme.test_cli.ThemeCliTests -v
node tests/theme/test_preview_browser.mjs /tmp/dynamic-theme-preview.html
```

Expected: CLI tests pass. Browser test passes when Chrome/Chromium is installed;
otherwise record the missing-browser environment and rely on CI.

- [ ] **Step 7: Commit preview injection**

```bash
git add tools/theme_native.cpp tools/smalltv_theme.py tools/theme_preview.py \
  tests/theme/test_cli.py tests/theme/test_preview_browser.mjs
git commit -m "feat(theme): preview injected dynamic values"
```

### Task 6: Add a Reproducible Dynamic Dashboard Example

**Files:**
- Create: `examples/themes/live-status/theme.json`
- Create: `examples/themes/live-status.stheme`
- Create: `examples/themes/live-status-preview.html`
- Modify: `examples/themes/README.md`
- Modify: `.github/workflows/build.yml`
- Modify: `tests/theme/test_pack.py`

**Interfaces:**
- Consumes: manifest schema and `--data` preview interface from Tasks 1–5.
- Produces: an editor/reference fixture demonstrating every newly shipped capability.

- [ ] **Step 1: Write a failing package/example test**

In `test_pack.py`, require `examples/themes/live-status/theme.json` to contain:

```python
def test_live_status_example_is_dynamic_and_reproducible(self):
    root = ROOT/'examples/themes/live-status'
    manifest = json.loads((root/'theme.json').read_text())
    layers = {layer['id']: layer for layer in manifest['layers']}
    self.assertEqual(layers['headline']['scroll']['mode'], 'loop')
    self.assertEqual(layers['headline-bounce']['scroll']['mode'], 'bounce')
    self.assertIn('width', layers['level-bar']['bind'])
    self.assertIn('fill', layers['level-bar']['bind'])
    self.assertGreater(layers['level-track']['cornerRadius'], 0)
    built = pack.build(root)
    self.assertEqual(built, (ROOT/'examples/themes/live-status.stheme').read_bytes())
```

- [ ] **Step 2: Run `test_pack.py` and verify RED**

Run:

```bash
python3 -m unittest tests.theme.test_pack -v
```

Expected: failure because `live-status` does not exist.

- [ ] **Step 3: Create the source manifest**

Use the documented example endpoint `http://192.168.1.10/status.json`, a 10-second
interval, and declared fields `label`, `level`, `state`. Include:

- a loop-scrolling `{status.label}` text viewport;
- a second bounce-scrolling label so both modes are copyable by authors/editors;
- a rounded track and a bar whose width maps level 0–100 to 0–200;
- threshold-bound bar fill with green/amber/red stops;
- a status text color bound to the numeric `status.level` field;
- static fallback geometry and colors that produce a valid first frame.

- [ ] **Step 4: Generate package and deterministic preview**

Run:

```bash
python3 tools/smalltv_theme.py build examples/themes/live-status examples/themes/live-status.stheme
python3 tools/smalltv_theme.py preview examples/themes/live-status \
  examples/themes/live-status-preview.html --seconds 8 --fps 15 \
  --time 2026-09-22T12:00:00 \
  --data status.label='SmallTV dynamic dashboard headline' \
  --data status.level=82 --data status.state=Warning
```

- [ ] **Step 5: Document and add CI reproducibility checks**

Add the example to `examples/themes/README.md`. Extend the theme CI step with:

```bash
python3 tools/theme_pack.py examples/themes/live-status /tmp/live-status.stheme
cmp examples/themes/live-status.stheme /tmp/live-status.stheme
```

- [ ] **Step 6: Run package, CLI, and native example tests**

Run:

```bash
python3 -m unittest tests.theme.test_pack tests.theme.test_cli -v
python3 tools/smalltv_theme.py validate examples/themes/live-status.stheme
```

Expected: package validates, preview contains multiple frames, rebuild is identical.

- [ ] **Step 7: Commit the example**

```bash
git add examples/themes/live-status examples/themes/live-status.stheme \
  examples/themes/live-status-preview.html examples/themes/README.md \
  .github/workflows/build.yml tests/theme/test_pack.py
git commit -m "feat(theme): add dynamic dashboard example"
```

### Task 7: Publish the Complete Authoring Contract

**Files:**
- Modify: `docs/src/content/docs/features/themes.md`
- Modify: `README.md` only if its feature summary needs one concise update
- Test: `tests/theme/test_cli.py`

**Interfaces:**
- Consumes: final parser names, ranges, behavior, CLI flags, and example from Tasks 1–6.
- Produces: public documentation the editor agent and theme authors can follow without reading firmware code.

- [ ] **Step 1: Add a failing documentation contract test**

In `test_cli.py`, read `docs/src/content/docs/features/themes.md` and assert it
contains all authoritative public tokens:

```python
for token in ('"mode": "loop"', '"mode": "bounce"', 'cornerRadius',
              'scroll.width', 'scroll.speed', 'color stops',
              '--data', 'live-status'):
    self.assertIn(token, documentation)
```

Also assert the property matrix contains rows for text, rectangle, circle, line,
image, and animation.

- [ ] **Step 2: Run the documentation test and verify RED**

Run:

```bash
python3 -m unittest tests.theme.test_cli.ThemeCliTests -v
```

Expected: missing-token assertions fail.

- [ ] **Step 3: Expand the public documentation**

Add sections that reproduce, without contradiction:

- the complete scroll object and viewport/anchor behavior;
- loop/bounce timelines and reset semantics;
- numeric mapping formula, rounding, clamp/extrapolation, safe ranges, and fallback;
- ordered color-stop selection;
- full layer/property matrix;
- `cornerRadius`, its effective clamp, and rounded stroke semantics;
- dirty-region/performance behavior;
- the `--data key=value` preview workflow;
- a complete live-status example and explicit “no history/line charts in V1” note;
- diagnostics users will see for invalid manifests.

- [ ] **Step 4: Run documentation and full host tests**

Run:

```bash
python3 -m unittest discover -s tests/theme -p 'test_*.py'
sh tests/theme/run.sh
node tests/theme/test_webui.js
```

Expected: all pass.

- [ ] **Step 5: Commit documentation**

```bash
git add docs/src/content/docs/features/themes.md README.md tests/theme/test_cli.py
git commit -m "docs(theme): document scrolling and dynamic properties"
```

Do not stage `README.md` if inspection shows no summary change is needed.

### Task 8: Regenerate, Build, Verify on Hardware, and Update the PR

**Files:**
- Modify: `src/webui.h` only if `src/webui.html` changed during implementation
- Modify: PR #15 description through GitHub CLI

**Interfaces:**
- Consumes: every preceding task.
- Produces: a verified SmallTV Pro artifact, hardware evidence, pushed commits, and a reviewable PR.

- [ ] **Step 1: Regenerate checked-in artifacts**

If `src/webui.html` changed, run `python3 tools/gzip_webui.py` and verify only the
expected generated `src/webui.h` changes. Rebuild all three `.stheme` examples and
compare them with the checked-in packages.

- [ ] **Step 2: Run the complete host validation from a clean state**

Run:

```bash
python3 -m unittest discover -s tests/theme -p 'test_*.py'
sh tests/theme/run.sh
node tests/theme/test_webui.js
python3 tools/theme_pack.py examples/themes/pixel-room /tmp/pixel-room.stheme
cmp examples/themes/pixel-room.stheme /tmp/pixel-room.stheme
python3 tools/theme_pack.py examples/themes/terminal-ops /tmp/terminal-ops.stheme
cmp examples/themes/terminal-ops.stheme /tmp/terminal-ops.stheme
python3 tools/theme_pack.py examples/themes/live-status /tmp/live-status.stheme
cmp examples/themes/live-status.stheme /tmp/live-status.stheme
git diff --check
```

Expected: zero failures and byte-identical packages.

- [ ] **Step 3: Build the production target**

Run:

```bash
/home/theophile/.platformio/penv/bin/pio run -e smalltv_esp32_8mb -t clean
/home/theophile/.platformio/penv/bin/pio run -e smalltv_esp32_8mb
```

Expected: SUCCESS; record flash/RAM totals and ensure the app remains within the
2,228,224-byte OTA slot.

- [ ] **Step 4: Install and exercise the dynamic example on hardware**

Upload the new `firmware.bin` to `http://192.168.1.153/update`, wait for
`/api/status` to report a software restart and `variant: esp32-pro`, then install
`live-status.stheme` through `/api/themes/install`. Point its URL at a controlled
LAN JSON fixture and visually verify:

- short text remains stationary;
- loop and bounce both move and pause correctly;
- a changed label restarts at the beginning;
- level 0, 50, and 100 resize the bar;
- threshold colors change at exact stop values;
- rounded corners remain correct at zero and narrow bar widths.

Do not claim hardware success if the controlled JSON fixture or visual check was
not actually performed; report the missing evidence instead.

- [ ] **Step 5: Request whole-branch review and address findings**

Compare `origin/main...HEAD`. The reviewer must focus on parser strictness,
wrap-safe timing, fetched-value safety, dirty-region completeness, scanline bounds,
and firmware heap/flash costs. Fix Critical and Important findings with a new
failing test before implementation, then rerun Steps 2–3.

- [ ] **Step 6: Commit any regenerated artifact changes**

```bash
git add src/webui.h examples/themes/*.stheme examples/themes/*-preview.html
git commit -m "chore(theme): regenerate dynamic theme artifacts"
```

Skip this commit when the worktree is already clean.

- [ ] **Step 7: Push and update PR #15**

Push `theme-engine-v1`, then update the PR description with:

- loop and bounce scrolling behavior;
- the dynamic-property and color-stop schema;
- rounded rectangle support;
- preview injection and live-status example;
- final host/build/hardware evidence;
- explicit V1 exclusions for history and line charts.

Finish by verifying `git status -sb`, the PR head SHA, and the rendered PR body.
