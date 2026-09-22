# Theme Clocks on Every Display Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make theme clocks build and run on every display target `CONTRIBUTING.md` requires (`smalltv`, `smalltv_c2`, `smalltv_esp32`, `smalltv_esp32_wg`, `smalltv_esp32_8mb`), reformat `src/features/theme/*` to the repo's usual style, and fix the doc/README items the maintainer's PR #15 review asked for.

**Architecture:** Widen the existing `WITH_THEME` compile flag from Pro-only to every non-lean/non-loader target in two steps (ESP32 family first, then ESP8266 once its fetcher no longer needs FreeRTOS), reformat the theme module file-by-file with a hard whitespace-only verification gate, then fix docs/README and do one final full-fleet measurement pass.

**Tech Stack:** C++11, ArduinoJson 7, Arduino/ESP32/ESP8266, PlatformIO, Astro docs.

**Spec:** `docs/superpowers/specs/2026-09-22-theme-every-target-design.md`

## Global Constraints

- `WITH_THEME` must compile on `smalltv`, `smalltv_c2`, `smalltv_esp32`, `smalltv_esp32_wg`, `smalltv_esp32_8mb`; `smalltv_lean` and `smalltv_loader` stay exempt (matching every other `WITH_*` feature).
- No behavior change to anything `tests/theme/*` covers (parser, resolver, scheduler, renderer, package format). This is a portability and style pass.
- The ESP8266 fetch path is one bounded, blocking HTTP call per main-loop tick — no FreeRTOS, no new concurrency model, matching `StockClient.cpp`/`RadarClient.cpp`.
- Reformat tasks change whitespace/line breaks and comment density only — never logic, names, or structure. Each reformat task's file(s) must satisfy `git diff --ignore-all-space --ignore-blank-lines -- <file>` reporting no non-comment token changes (see each task's exact verification step).
- Report flash and static RAM for every board this reaches; state which boards actually ran versus only built (`CONTRIBUTING.md`: "An untested board is fine, an unstated one is not").
- The settings-atomicity split (`AtomicJson.h`/`SettingsTransaction.h`) is already done as PR #18 and is out of scope here.
- Every commit lands on the existing `theme-engine-v1` branch, updating PR #15 in place — never a new branch or PR.
- Do not change `FW_VERSION` in `src/config.h`.

## Review Focus

- A reformat that looks like pure whitespace but actually changes operator precedence, drops a statement, or reorders a side-effecting expression — the `--ignore-all-space` diff check in each reformat task's own steps is what pins this, but a final task-9 full-suite rerun is the backstop.
- `WITH_THEME` silently turning on for `smalltv_lean` (it `extends = env:smalltv`, so a careless `config.h` condition change enables it there too) — Task 2 pins this with an explicit `-D WITH_THEME=0` and a build that confirms the lean image's size is unaffected.
- The ESP8266 fetcher blocking the main loop for the full `DataTimeoutMs` (3000 ms) plus body-read time on every attempt, not just failures — nothing exercises this on real hardware in this plan; Task 2 states that gap explicitly rather than assuming it away.
- `smalltv_esp32`/`smalltv_esp32_wg` (the tightest OTA slots, already at 90.4%/93.3% before theme) overflowing once the ESP8266 fetcher port and every reformat is applied on top of the flash cost already measured — Task 10 re-measures every board after all other tasks, not just after Task 1.
- A docs/README edit breaking `tests.theme.test_cli.ThemeCliTests.test_documentation_covers_scrolling_and_dynamic_properties`, which asserts specific literal tokens exist in `themes.md` — Task 9 reruns that exact test after every doc edit.

---

### Task 1: Enable Theme Support on the ESP32 Family

**Files:**
- Modify: `src/config.h` (the `WITH_THEME` block, currently lines 396-403)
- Modify: `platformio.ini` (`[env:smalltv_c2]` and `[env:smalltv_esp32]` `build_src_flags`)

**Interfaces:**
- Produces: `WITH_THEME` compiles to `1` on `smalltv_c2`, `smalltv_esp32`, `smalltv_esp32_wg` (inherits from `smalltv_esp32` via `extends`), and unchanged on `smalltv_esp32_8mb`. Still `0` on ESP8266 targets pending Task 2.

- [ ] **Step 1: Widen the WITH_THEME condition to the ESP32 family**

In `src/config.h`, replace:

```c
// Theme packages target the Pro filesystem and flash budget in V1.
#ifndef WITH_THEME
#ifdef SMALLTV_ESP32_PRO
#define WITH_THEME 1
#else
#define WITH_THEME 0
#endif
#endif
```

with:

```c
// Theme packages run on every ESP32-family target; the ESP8266 needs its
// fetcher ported off FreeRTOS first (see ThemeDataClient.cpp).
#ifndef WITH_THEME
#if defined(SMALLTV_ESP32)||defined(SMALLTV_ESP32C2)
#define WITH_THEME 1
#else
#define WITH_THEME 0
#endif
#endif
```

`SMALLTV_ESP32_PRO` builds already define `SMALLTV_ESP32` too (see
`[env:smalltv_esp32_8mb]`'s `build_flags`), so this condition already covers
the Pro — no behavior change there.

- [ ] **Step 2: Add the theme include path to smalltv_c2 and smalltv_esp32**

In `platformio.ini`, `[env:smalltv_c2]`'s `build_src_flags` currently reads:

```
build_src_flags =
	-iquote src
	-iquote src/features/ticker
	-iquote src/features/usage
	-iquote src/features/radar
	-iquote src/features/notify
	-iquote src/features/ha
```

Add `-iquote src/features/theme` as a new line in that list. Make the
identical addition to `[env:smalltv_esp32]`'s `build_src_flags` (same six
lines, same fix). Do **not** edit `[env:smalltv_esp32_wg]`: it `extends =
env:smalltv_esp32` and does not redeclare `build_src_flags`, so it inherits
the fix automatically (PlatformIO only replaces a list the child env
redeclares).

- [ ] **Step 3: Build and measure every affected ESP32-family target**

Run each of these (the C2 build recompiles the whole framework the first
time since it uses its own `custom_sdkconfig`/MCU — expect 20-40 minutes for
that one build only):

```bash
/home/theophile/.platformio/penv/bin/pio run -e smalltv_c2
/home/theophile/.platformio/penv/bin/pio run -e smalltv_esp32
/home/theophile/.platformio/penv/bin/pio run -e smalltv_esp32_wg
/home/theophile/.platformio/penv/bin/pio run -e smalltv_esp32_8mb
```

Expected: all four `SUCCESS`. Record each one's `RAM:`/`Flash:` summary line
(percentage and bytes used/free) — these are the numbers Task 10 assembles
into the PR update.

- [ ] **Step 4: Commit**

```bash
git add src/config.h platformio.ini
git commit -m "feat(theme): enable theme support on the ESP32 family"
```

---

### Task 2: Port the ESP8266 Data Fetcher and Enable Theme There

**Files:**
- Modify: `src/features/theme/ThemeDataClient.cpp`
- Modify: `src/config.h` (finish widening `WITH_THEME`)
- Modify: `platformio.ini` (`[env:smalltv]` `build_src_flags`, `[env:smalltv_lean]` `build_flags`)

**Interfaces:**
- Consumes: `fetchThemeSource(DataFetch&)` (existing, unchanged, in the file's anonymous namespace), `smalltv::DataFetch::finish(bool)` (existing, `ThemeData.h`).
- Produces: `startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>&) -> bool`, now with two implementations selected by `SMALLTV_ESP8266`. No change to its signature or to any caller (`ThemeMode.cpp`).

- [ ] **Step 1: Guard the FreeRTOS-only includes and add the ESP8266 branch**

In `src/features/theme/ThemeDataClient.cpp`, the includes currently read:

```cpp
#include "ThemeDataClient.h"
#if WITH_THEME
#include "Platform.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
```

Change to:

```cpp
#include "ThemeDataClient.h"
#if WITH_THEME
#include "Platform.h"
#include <ArduinoJson.h>
#if !defined(SMALLTV_ESP8266)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include <new>
```

`fetchThemeSource`, `Deadline`, `BoundedClient`, `BodyStream`, `jsonValue`,
and `jsonPath` (everything above `void runFetch(...)` in the anonymous
namespace) do not change.

`runFetch` is FreeRTOS-only (it calls `vTaskDelete`); wrap it:

```cpp
#if !defined(SMALLTV_ESP8266)
void runFetch(void* parameter) {
  {
    std::unique_ptr<std::shared_ptr<smalltv::DataFetch>> context(
        static_cast<std::shared_ptr<smalltv::DataFetch>*>(parameter));
    auto request=std::move(*context);context.reset();
    request->finish(fetchThemeSource(*request));
  } // release every C++ object before FreeRTOS deletes the task's stack
  vTaskDelete(nullptr);
}
#endif
}
```

Finally, replace the single `startThemeDataFetch` definition at the bottom
of the file:

```cpp
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  constexpr uint32_t StackBytes=8192;
  if(ESP.getFreeHeap()<StackBytes+18000) return false;
  auto context=new(std::nothrow) std::shared_ptr<smalltv::DataFetch>(request);
  if(!context) return false;
  if(xTaskCreate(runFetch,"theme-json",StackBytes,context,1,nullptr)!=pdPASS) {
    delete context;return false;
  }
  return true;
}
#endif
```

with:

```cpp
#if defined(SMALLTV_ESP8266)
// No FreeRTOS on this chip: run the bounded fetch synchronously on the
// caller's own stack, the same "one blocking call per main-loop tick"
// pattern the ticker and radar clients already use here. Deadline (inside
// fetchThemeSource) already bounds this to DataTimeoutMs plus body-read
// time, the same order of magnitude a TLS handshake already costs this
// chip elsewhere.
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  request->finish(fetchThemeSource(*request));
  return true;
}
#else
bool startThemeDataFetch(const std::shared_ptr<smalltv::DataFetch>& request) {
  constexpr uint32_t StackBytes=8192;
  if(ESP.getFreeHeap()<StackBytes+18000) return false;
  auto context=new(std::nothrow) std::shared_ptr<smalltv::DataFetch>(request);
  if(!context) return false;
  if(xTaskCreate(runFetch,"theme-json",StackBytes,context,1,nullptr)!=pdPASS) {
    delete context;return false;
  }
  return true;
}
#endif
#endif
```

`ThemeData.h`'s `DataRequests::take()` already returns a result the instant
`finished()` is true, so no change is needed there or in `ThemeMode.cpp`:
calling `start()` then immediately `finish()`-ing inside
`startThemeDataFetch` means the very next `take()` (on the next `service()`
call) sees a completed request, exactly like a same-tick FreeRTOS task that
happened to finish instantly.

- [ ] **Step 2: Finish widening WITH_THEME to every non-lean target**

In `src/config.h`, replace the block Task 1 left:

```c
// Theme packages run on every ESP32-family target; the ESP8266 needs its
// fetcher ported off FreeRTOS first (see ThemeDataClient.cpp).
#ifndef WITH_THEME
#if defined(SMALLTV_ESP32)||defined(SMALLTV_ESP32C2)
#define WITH_THEME 1
#else
#define WITH_THEME 0
#endif
#endif
```

with:

```c
// Theme packages are supported on every display target except the lean
// ESP8266 image, which opts out explicitly (see [env:smalltv_lean] in
// platformio.ini) to keep the heap headroom it exists to protect.
#ifndef WITH_THEME
#define WITH_THEME 1
#endif
```

- [ ] **Step 3: Add the theme include path to smalltv, keep it off for smalltv_lean**

In `platformio.ini`, `[env:smalltv]`'s `build_src_flags` currently reads:

```
build_src_flags =
	-iquote src
	-iquote src/features/ticker
	-iquote src/features/usage
	-iquote src/features/radar
	-iquote src/features/notify
	-iquote src/features/ha
```

Add `-iquote src/features/theme` as a new line. `[env:smalltv_lean]`
`extends = env:smalltv` and does not redeclare `build_src_flags`, so it
inherits this unchanged — harmless, since no theme `.cpp` compiles there
once Step 4 turns the feature off.

`[env:smalltv_lean]`'s `build_flags` currently reads:

```
build_flags =
	-D SMALLTV_ESP8266
	-D SMALLTV_LEAN
	-D PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY
	-D ARDUINOJSON_USE_DOUBLE=0
	-D WITH_HA=0
	-D WITH_USAGE=0
```

Add `-D WITH_THEME=0` as a new line in that list.

- [ ] **Step 4: Build and measure both ESP8266 images**

```bash
/home/theophile/.platformio/penv/bin/pio run -e smalltv
/home/theophile/.platformio/penv/bin/pio run -e smalltv_lean
```

Expected: both `SUCCESS`. `smalltv`'s `RAM:`/`Flash:` numbers are new
(previously theme could not compile here at all — the build failed on
`<freertos/task.h>` — so this is the first real measurement, not a delta).
`smalltv_lean`'s numbers should be unchanged from its pre-this-plan baseline
(theme stays compiled out there); if they differ, `WITH_THEME` leaked in —
stop and find why before continuing.

- [ ] **Step 5: Commit**

```bash
git add src/features/theme/ThemeDataClient.cpp src/config.h platformio.ini
git commit -m "feat(theme): port the ESP8266 fetcher off FreeRTOS"
```

---

### Task 3: Reformat ThemeCatalog.h and ThemeData.h

**Files:**
- Modify: `src/features/theme/ThemeCatalog.h`
- Modify: `src/features/theme/ThemeData.h`

**Interfaces:** None — formatting only, no declarations change.

- [ ] **Step 1: Reformat to repo style**

Apply throughout both files, matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

Worked example (`ThemeData.h`, `DataFetch::finish`), before:

```cpp
void finish(bool ok) { success=ok;finished_.store(true,std::memory_order_release); }
```

after:

```cpp
void finish(bool ok) {
  success = ok;
  finished_.store(true, std::memory_order_release);
}
```

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemeCatalog.h src/features/theme/ThemeData.h
```

Expected: empty output. Any remaining diff line is a token-level change —
find it and revert it before continuing; this task must not change behavior.

- [ ] **Step 3: Run the full native suite**

```bash
sh tests/theme/run.sh
```

Expected: identical output to before this task (`theme engine tests
passed`, ..., `theme catalog tests passed`, both example packages render).

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemeCatalog.h src/features/theme/ThemeData.h
git commit -m "style(theme): reformat ThemeCatalog and ThemeData to repo style"
```

---

### Task 4: Reformat ThemeDataClient.h and ThemeDataClient.cpp

**Files:**
- Modify: `src/features/theme/ThemeDataClient.h`
- Modify: `src/features/theme/ThemeDataClient.cpp`

**Interfaces:** None — formatting only. This is the file Task 2 already
touched; reformat its Task-2 content the same as everything else in it.

- [ ] **Step 1: Reformat to repo style**

Apply throughout the file(s), matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

Worked example (`ThemeDataClient.cpp`,
`fetchThemeSource`'s HTTPS branch), before:

```cpp
if(request.source.url.rfind("https://",0)==0) {
  if(ESP.getFreeHeap()<18000) return false;
  auto secure=new(std::nothrow) BoundedClient<SecureClient>(deadline);
  if(!secure) return false;
```

after:

```cpp
if (request.source.url.rfind("https://", 0) == 0) {
  if (ESP.getFreeHeap() < 18000) return false;
  auto secure = new(std::nothrow) BoundedClient<SecureClient>(deadline);
  if (!secure) return false;
```

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemeDataClient.h src/features/theme/ThemeDataClient.cpp
```

Expected: empty output.

- [ ] **Step 3: Run the full native suite and rebuild the ESP8266 target**

```bash
sh tests/theme/run.sh
/home/theophile/.platformio/penv/bin/pio run -e smalltv
```

Expected: native suite output unchanged; `smalltv` `SUCCESS` with the same
`RAM:`/`Flash:` numbers Task 2 recorded (reformatting must not move them).

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemeDataClient.h src/features/theme/ThemeDataClient.cpp
git commit -m "style(theme): reformat ThemeDataClient to repo style"
```

---

### Task 5: Reformat ThemeMode.h and ThemeMode.cpp

**Files:**
- Modify: `src/features/theme/ThemeMode.h`
- Modify: `src/features/theme/ThemeMode.cpp`

**Interfaces:** None — formatting only.

- [ ] **Step 1: Reformat to repo style**

Apply throughout the file(s), matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemeMode.h src/features/theme/ThemeMode.cpp
```

Expected: empty output.

- [ ] **Step 3: Run the full native suite**

```bash
sh tests/theme/run.sh
```

Expected: identical output to before this task.

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemeMode.h src/features/theme/ThemeMode.cpp
git commit -m "style(theme): reformat ThemeMode to repo style"
```

---

### Task 6: Reformat ThemeWeb.h and ThemeWeb.cpp

**Files:**
- Modify: `src/features/theme/ThemeWeb.h`
- Modify: `src/features/theme/ThemeWeb.cpp`

**Interfaces:** None — formatting only.

- [ ] **Step 1: Reformat to repo style**

Apply throughout the file(s), matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemeWeb.h src/features/theme/ThemeWeb.cpp
```

Expected: empty output.

- [ ] **Step 3: Run the full native suite**

```bash
sh tests/theme/run.sh
```

Expected: identical output to before this task.

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemeWeb.h src/features/theme/ThemeWeb.cpp
git commit -m "style(theme): reformat ThemeWeb to repo style"
```

---

### Task 7: Reformat ThemePackage.h and ThemePackage.cpp

**Files:**
- Modify: `src/features/theme/ThemePackage.h`
- Modify: `src/features/theme/ThemePackage.cpp`

**Interfaces:** None — formatting only.

- [ ] **Step 1: Reformat to repo style**

Apply throughout the file(s), matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemePackage.h src/features/theme/ThemePackage.cpp
```

Expected: empty output.

- [ ] **Step 3: Run the full native suite**

```bash
sh tests/theme/run.sh
```

Expected: identical output to before this task (this exercises the packer,
both example `.stheme` files, and `check_example`).

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemePackage.h src/features/theme/ThemePackage.cpp
git commit -m "style(theme): reformat ThemePackage to repo style"
```

---

### Task 8: Reformat ThemeEngine.h and ThemeEngine.cpp

**Files:**
- Modify: `src/features/theme/ThemeEngine.h`
- Modify: `src/features/theme/ThemeEngine.cpp`

**Interfaces:** None — formatting only. This is the largest file (~1,000
lines); do it last and expect it to take longest.

- [ ] **Step 1: Reformat to repo style**

Apply throughout the file(s), matching `Settings.cpp`/`Clock.cpp`'s density:

- One statement per line (split `a;b;` onto two lines).
- A space after every `,` and around every binary operator (`a+b` → `a + b`,
  `a<b?c:d` → `a < b ? c : d`).
- A space after control-flow keywords (`if(x)` → `if (x)`, `for(...)` →
  `for (...)`).
- Keep braces on their current line-opening style (the codebase already uses
  `if (x) {` / `}` — don't invent a new brace convention).
- Add a comment only where the file currently has none but a neighboring
  file would (e.g. `Settings.cpp` comments a non-obvious constraint like a
  clamp range or a cross-file invariant); don't add a comment that would
  just restate the line.

Worked example (`ThemeEngine.cpp`,
`resolveNumericBinding`'s clamp branch), before:

```cpp
static int clampInteger(int value,int lo,int hi) {
  return std::max(lo,std::min(value,hi));
}
```

after:

```cpp
static int clampInteger(int value, int lo, int hi) {
  return std::max(lo, std::min(value, hi));
}
```

Because this file is large, reformat it in logical chunks (parser, resolver,
scroll scheduler, renderer) and run Step 2/3 after each chunk rather than
waiting until the whole file is done — a whitespace-only diff failure is
much easier to localize in 200 lines than in 1,000.

- [ ] **Step 2: Verify the reformat changed only whitespace**

```bash
git diff --ignore-all-space --ignore-blank-lines -- src/features/theme/ThemeEngine.h src/features/theme/ThemeEngine.cpp
```

Expected: empty output.

- [ ] **Step 3: Run the full native suite**

```bash
sh tests/theme/run.sh
```

Expected: identical output to before this task. This is the suite that
exercises scroll timing, bindings, rounded-rect rendering — the highest-risk
file to accidentally change while reformatting, so do not skip this step
even if Step 2 already passed.

- [ ] **Step 4: Commit**

```bash
git add src/features/theme/ThemeEngine.h src/features/theme/ThemeEngine.cpp
git commit -m "style(theme): reformat ThemeEngine to repo style"
```

---

### Task 9: Documentation and README Fixes

**Files:**
- Modify: `README.md`
- Modify: `docs/astro.config.mjs`
- Modify: `docs/src/content/docs/features/themes.md`
- Test: `tests/theme/test_cli.py` (existing test, rerun only — no new test)

**Interfaces:** None — prose only.

- [ ] **Step 1: Fix the README theme bullet**

`README.md` currently has (one line):

```
- **Custom theme clocks on SmallTV Pro.** Install declarative `.stheme` packages from the Display tab: background images, animated sprites, clock/date text, simple shapes, and bounded JSON data fields such as `{weather.temp}`. Themes live in storage and need no firmware recompilation. Notifications remain above them. Start with [Pixel Room](examples/themes/pixel-room.stheme) or the included offline previews. Follow the [theme authoring guide](docs/src/content/docs/features/themes.md). SmallTV Studio is maintained as a separate editor project.
```

Replace it with:

```
- **Custom theme clocks.** Install declarative `.stheme` packages from the Display tab: background images, animated sprites, clock/date text, scrolling text, simple and rounded shapes, and bounded JSON data fields such as `{weather.temp}`. Themes live in storage and need no firmware recompilation. Notifications remain above them. Start with [Pixel Room](examples/themes/pixel-room.stheme) or the included offline previews. Follow the [theme authoring guide](https://giovi321.github.io/smalltv-mod/features/themes/).
```

This drops "on SmallTV Pro" (no longer Pro-only), drops the "SmallTV Studio
is maintained as a separate editor project" sentence (per the maintainer:
that project isn't part of this repo), and points the guide link at the
published docs site instead of the Markdown source, matching every other
feature bullet in this list.

- [ ] **Step 2: Fix the sidebar label**

`docs/astro.config.mjs` currently has:

```js
{ label: 'Theme clocks (Pro)', link: '/features/themes/' },
```

Replace with:

```js
{ label: 'Theme clocks', link: '/features/themes/' },
```

- [ ] **Step 3: Add the LAN-URL trust note to the theme guide**

In `docs/src/content/docs/features/themes.md`, in the "Small JSON data
sources" section, immediately after the paragraph ending "...an HTTPS source
must explicitly set `"insecureTls": true`... Data fetching is owned by
theme mode and does not affect notifications or other display modes."
(this paragraph already exists), add a new paragraph:

```markdown
Installing a theme installs its data sources too: the device will
periodically poll every URL the manifest declares, including addresses on
your LAN. Only install themes from sources you trust — a manifest's `data`
block is not sandboxed beyond the request limits above (2 KiB response, one
in-flight request, three-second deadline).
```

- [ ] **Step 4: Rerun the documentation contract test**

```bash
python3 -m unittest tests.theme.test_cli.ThemeCliTests.test_documentation_covers_scrolling_and_dynamic_properties -v
```

Expected: `ok`. This test asserts specific literal tokens exist in
`themes.md` (`"mode": "loop"`, `cornerRadius`, `--data`, `live-status`,
etc.) — Step 3's edit must not remove any of them. If it fails, the new
paragraph landed in a way that disturbed surrounding content; fix and rerun.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/astro.config.mjs docs/src/content/docs/features/themes.md
git commit -m "docs(theme): fix Studio/link references and add a LAN-trust note"
```

---

### Task 10: Full-Fleet Verification and PR Update

**Files:** None created or modified beyond what earlier tasks already
changed; this task builds, measures, and updates PR #15's description.

**Interfaces:**
- Consumes: every file Tasks 1-9 touched.
- Produces: a pushed `theme-engine-v1` and an updated PR #15 description with final numbers.

- [ ] **Step 1: Run the complete host validation from a clean state**

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

Expected: zero failures, byte-identical packages.

- [ ] **Step 2: Rebuild and re-measure every listed target**

```bash
/home/theophile/.platformio/penv/bin/pio run -t clean -e smalltv -e smalltv_lean -e smalltv_c2 -e smalltv_esp32 -e smalltv_esp32_wg -e smalltv_esp32_8mb
/home/theophile/.platformio/penv/bin/pio run -e smalltv -e smalltv_lean -e smalltv_c2 -e smalltv_esp32 -e smalltv_esp32_wg -e smalltv_esp32_8mb
```

Expected: all six `SUCCESS`. Record each target's final `RAM:`/`Flash:`
line — these supersede the per-task numbers from Tasks 1-2 (the reformat
tasks should not have moved them, but this is the number that goes in the
PR). Compare `smalltv_lean`'s figures against its pre-this-plan baseline to
confirm theme stayed compiled out.

- [ ] **Step 3: Exercise the theme feature on the Pro hardware**

Using the same live-device workflow as the original PR #15 verification
(upload the fresh `smalltv_esp32_8mb` firmware.bin to the device's `/update`,
confirm a fresh boot via `/api/status`, install and select `live-status`,
drive a controlled LAN JSON fixture through the visual checklist: short text
stays put, loop/bounce move and pause, a changed label restarts scrolling,
level 0/50/100 resize the bar, threshold colors flip at the exact stops,
rounded corners stay correct at zero/narrow widths). This confirms the
reformat and multi-board changes did not regress the one target that was
already fully verified.

- [ ] **Step 4: Push and update PR #15**

```bash
git push git@github.com:kittyruntime/smalltv-mod.git theme-engine-v1
```

(Use this explicit URL form if the `fork` remote's configured host alias
does not resolve in the current environment — verify with `git remote -v`
and `ssh -T git@github.com` first, matching the address in that
`ssh -T` output.)

Update the PR description via `gh pr edit 15 --repo giovi321/smalltv-mod
--body "..."` to state, for each of the five requested changes: what
changed, the per-board flash/RAM numbers from Step 2, which boards actually
ran (the Pro, on hardware) versus only built (the rest), and the one
explicitly unmeasured item — ESP8266 free heap during rendering and during a
concurrent stock-ticker TLS fetch, which needs real ESP8266 hardware this
environment does not have.

- [ ] **Step 5: Verify the final state**

```bash
git status -sb
gh pr view 15 --repo giovi321/smalltv-mod --json headRefOid,url
```

Confirm the pushed branch's HEAD SHA matches the PR's `headRefOid`, and read
the rendered PR body at the returned URL to confirm it matches what Step 4
posted.
