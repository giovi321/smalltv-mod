# Theme Clocks on Every Display Target

## Status

Proposed design responding to the maintainer's PR #15 review, formalized in
`CONTRIBUTING.md`: "Features reach every display target." Theme clocks
currently build only for `smalltv_esp32_8mb` (SmallTV Pro); this closes that
gap for every target except the two the repo's own convention exempts.

## Motivation

The maintainer's review of PR #15:

> I can't merge it in its current scope, though. A feature has to reach every
> device the firmware supports... A board-specific limit is accepted only
> where the hardware cannot support the feature... Porting effort is not a
> hardware limit.

`CONTRIBUTING.md` lists the requested changes precisely:

1. Build themes for `smalltv`, `smalltv_c2`, `smalltv_esp32`,
   `smalltv_esp32_wg`, and `smalltv_esp32_8mb` (`smalltv_lean` and
   `smalltv_loader` stay exempt, matching every other feature).
2. Port `ThemeDataClient.cpp`'s fetcher off FreeRTOS for the ESP8266.
3. Reformat `src/features/theme/*` to the repo's usual layout and comment
   density.
4. Move the atomic-settings-save change to its own PR.
5. Fix the README/docs references to SmallTV Studio and the theme guide link.
6. Document that an installed theme can poll any URL in its manifest,
   including LAN addresses.

Item 4 is already done: extracted to PR #18
(`feat(settings): save settings atomically with rollback`), reverted out of
`theme-engine-v1`. This spec covers items 1, 2, 3, 5, and 6, all landing as
further commits on `theme-engine-v1`, updating PR #15 in place — not a new
branch or PR.

## Goals

- `WITH_THEME` compiles and runs on every target `CONTRIBUTING.md` lists,
  with no behavioral change on `smalltv_esp32_8mb`.
- The ESP8266 fetch path follows the same non-blocking-per-tick convention
  the ticker (`StockClient.cpp`) and radar clients already use — no new
  concurrency model.
- Every affected board's flash and static RAM cost is measured and reported;
  `smalltv_esp32`/`smalltv_esp32_wg` (the tightest OTA slots) are the ones
  that decide feasibility.
- `src/features/theme/*` reads like the rest of the codebase.
- The doc/README items `CONTRIBUTING.md` lists are fixed.

## Non-goals

- Changing the theme manifest format, renderer, or any behavior verified by
  the existing native test suite (`tests/theme/*`). This is a portability and
  style pass, not a feature change.
- A true asynchronous/chunked ESP8266 HTTP client. See "ESP8266 fetcher"
  below for why the bounded-blocking-call pattern is sufficient and matches
  house convention.
- Real hardware heap measurement on ESP8266, absent access to a physical
  device (see "Verification" below — this gap is reported, not estimated
  away).

## Multi-board build configuration

`WITH_THEME` (`src/config.h`) currently reads:

```c
#ifndef WITH_THEME
#ifdef SMALLTV_ESP32_PRO
#define WITH_THEME 1
#else
#define WITH_THEME 0
#endif
#endif
```

Change the `#else` branch's default to `1`, so every target compiles theme
support unless it explicitly opts out (matching `WITH_TICKER`/`WITH_HA`/etc.,
none of which key off a specific board). `smalltv_lean` sets `WITH_THEME=0`
explicitly, the same way it already drops `WITH_HA`/`WITH_USAGE`; add it to
that env's `build_flags` in `platformio.ini`. `smalltv_loader` never compiles
feature code at all (`build_src_filter = -<*> +<loader.cpp>`), so it needs no
change.

`platformio.ini`: add `-iquote src/features/theme` to `build_src_flags` for
every env this reaches (`smalltv`, `smalltv_c2`, `smalltv_esp32`,
`smalltv_esp32_wg`) — currently only `smalltv_esp32_8mb` has it.
`smalltv_esp32_wg` inherits `smalltv_esp32`'s `build_src_flags` through
`extends`, so it needs no separate edit if `smalltv_esp32`'s list is complete
(confirm this holds; `extends` replaces rather than merges, per the existing
comment in that env block).

No other build-file changes are anticipated: `ThemePackage.h`'s `MaxPackage`
is already clamped to actual free space by `ThemeWeb.cpp`
(`std::min(MaxPackage, free-reserve)`), so a smaller LittleFS (ESP8266: 1 MB,
versus the Pro's ~3.7 MB) is already handled without a board-specific
constant.

## ESP8266 fetcher

`ThemeDataClient.cpp` currently spawns a FreeRTOS task
(`xTaskCreate(runFetch, ...)`) to run `fetchThemeSource()`, which is
unavailable on the ESP8266 Arduino core. The house convention for a periodic
network fetch that must not stall the display indefinitely is already
established in `StockClient.cpp`/`RadarClient.cpp`: **one bounded, blocking
HTTP call per main-loop service tick**, not a chunked async state machine —
`stepSymbol()` calls `fetchUrl()` synchronously, and the surrounding
`stocksService()` only decides *which* symbol is due next, spreading multiple
symbols' fetches across ticks so the loop keeps running between them. Theme
data has no multi-symbol scheduling need (`DataRequests` already allows only
one in-flight fetch), so this collapses to: call the existing
`fetchThemeSource()` directly, synchronously, from `startThemeDataFetch()`.

Design: give `ThemeDataClient.cpp` two implementations of
`startThemeDataFetch()`, selected by `#ifdef SMALLTV_ESP8266` /
`#else`:

- **ESP32/C2 (unchanged):** the existing FreeRTOS task path.
- **ESP8266 (new):** call `fetchThemeSource(*request)` directly on the
  caller's stack, then `request->finish(result)`, then return `true`
  unconditionally (matching the "did the attempt start" contract the
  existing `bool` return already has — here it always "starts" because it
  already finished). No changes to `ThemeData.h`'s `DataFetch`/`DataRequests`
  or to `ThemeMode.cpp`: `DataRequests::take()` already tolerates a request
  that is `finished()` the instant after `start()`.
- `fetchThemeSource()`, currently in `ThemeDataClient.cpp`'s anonymous
  namespace, does not change; both branches call the same function.
  `BoundedClient`/`Deadline` (the existing `DataTimeoutMs`-bounded transport
  wrapper) already bound the call's worst-case duration, so the main loop
  stalls for at most `DataTimeoutMs` (3000 ms) plus body-read time — the same
  order of magnitude the ticker's TLS handshakes already impose on this chip.

This is a same-file, mechanically small change: no new headers, no new
abstractions, no change to the parser/renderer/scheduler the existing native
test suite covers.

## Code style reformat

`src/features/theme/*.{h,cpp}` (~1,700 lines: `ThemeEngine`, `ThemePackage`,
`ThemeData`, `ThemeDataClient`, `ThemeMode`, `ThemeCatalog`, `ThemeWeb`) is
written far denser than the rest of the codebase — many statements per line,
minimal spacing, comments only where a non-obvious invariant demands one.
`Settings.cpp`/`Clock.cpp` are the reviewer's named reference: one statement
per line as the default, spaces around binary operators and after commas,
brace style and comment density matching the surrounding non-theme code.

This is a **formatting pass, not a behavior change**. Approach:

- One file at a time (start with the smallest — `ThemeCatalog.h` — and work
  up to `ThemeEngine.cpp`, the largest).
- Reformat only — no renaming, no restructuring, no logic changes — so a
  diff review can confirm "same code, different shape" instead of having to
  re-verify semantics.
- Re-run the full native suite (`sh tests/theme/run.sh`) after each file;
  a formatting pass that changes byte-for-byte test output has introduced a
  behavior change and needs to be found before moving to the next file.
- Comment density: match what similar logic gets elsewhere (e.g. a
  wrap-safe-timing comment belongs at the same density as `Clock.cpp`'s own
  wrap-safe comments), not a blanket increase.

## Documentation and README fixes

- `README.md`: remove "SmallTV Studio is maintained as a separate editor
  project" (introduced in the original theme commit; not accurate for a repo
  bullet, per the maintainer). Change the theme guide link from
  `docs/src/content/docs/features/themes.md` to the published
  `https://giovi321.github.io/smalltv-mod/features/themes/`, matching every
  other feature bullet's link style.
- `docs/astro.config.mjs`: sidebar label `'Theme clocks (Pro)'` →
  `'Theme clocks'` (no longer Pro-only).
- `docs/src/content/docs/features/themes.md`: add a note, near the `data`
  sources section, that an installed theme's manifest can make the device
  poll any URL it declares, including LAN addresses — install themes only
  from sources you trust. This is a trust-boundary note, not a new
  validation rule; nothing in the parser changes.
- `README.md`'s theme bullet: drop "on SmallTV Pro" now that it is not
  Pro-only.

## Verification

Per `CONTRIBUTING.md`'s "Building and testing" section:

- Build every listed target: `smalltv`, `smalltv_c2`, `smalltv_esp32`,
  `smalltv_esp32_wg`, `smalltv_esp32_8mb`.
- Report flash and static RAM for each. Already measured on the ESP32
  targets by isolating `WITH_THEME`'s cost (`PLATFORMIO_BUILD_FLAGS="-D
  WITH_THEME=1"` against the default build): **+68,248 B flash / +392 B
  static RAM** on `smalltv_esp32` (95.1% of the 1,572,864 B OTA slot used,
  77,746 B free) and the same combined with WireGuard on
  `smalltv_esp32_wg` (98.0% used, 31,234 B free). Both fit. `smalltv_c2` and
  `smalltv_esp32_8mb` are expected to fit with more room (larger slots); confirm
  with the same isolated measurement once the port compiles there.
- ESP8266 (`smalltv`): flash should be measurable the same way once the
  fetcher port lands (currently the build fails outright on
  `<freertos/task.h>`, which is itself evidence `WITH_THEME` never reached
  this target before). **RAM is the real risk** — baseline `smalltv` is
  already at 69.3% (25,112 B DRAM free), and `.bss`/`.data` deltas don't
  capture the fetcher's dynamic allocation (JSON parsing, `DataFetch`
  strings/vectors) that matters most on this chip.
- **Heap-under-load** (`CONTRIBUTING.md`: "report free heap during rendering
  and during a data fetch while the stock ticker's TLS client is active") —
  I have no ESP8266 hardware to measure this on. State that explicitly in the
  PR rather than estimating a number and presenting it as measured, per
  `CONTRIBUTING.md`'s own rule: "An untested board is fine, an unstated one
  is not."
- State which boards actually ran (the Pro, on the user's real device) versus
  which only built.

## Testing strategy

- No changes to `tests/theme/*` are anticipated for the build-flag and
  fetcher-port work — those tests already exercise `ThemeEngine`/
  `ThemePackage`/rendering identically regardless of target, and
  `ThemeDataClient.cpp`'s ESP8266 branch is Arduino-only code with no native
  test double today (the existing tests exercise `fetchThemeSource`-adjacent
  logic like `readDataBody` directly, target-independent).
- The reformat pass's correctness gate is the full native suite passing
  byte-identically (same assertions, same example package renders) after
  each file.
- New/changed doc content gets no new automated test beyond what
  `test_documentation_covers_scrolling_and_dynamic_properties` already
  covers for the existing scroll/binding docs; the LAN-trust note and README
  fixes are prose, not tokens that test asserts on.

## Open risk

`smalltv_c2` and `smalltv_esp32_8mb` are not yet measured with `WITH_THEME`
forced on (the Pro already ships it by default; C2 needs the same isolated
`PLATFORMIO_BUILD_FLAGS` measurement `smalltv_esp32` got). Do this before
writing the PR's numbers, not after.
