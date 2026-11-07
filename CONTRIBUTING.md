# Contributing

## Features reach every display target

A new feature must build and run on every display target:

| Target | Hardware |
|---|---|
| `smalltv` | GeekMagic SmallTV and SmallTV-ultra (ESP8266) |
| `smalltv_c2` | SmallTV knockoff (ESP32-C2) |
| `smalltv_esp32` | NMMiner NM-TV-154 (ESP32) |
| `smalltv_esp32_wg` | NMMiner NM-TV-154 with WireGuard (ESP32) |
| `smalltv_esp32_8mb` | SmallTV Pro (ESP32, 8 MB flash) |

`smalltv_lean` and `smalltv_loader` are exempt. The lean image exists to drop features and free heap on the ESP8266, and the loader is a minimal installer with no display modes.

The reason is maintenance. Each feature limited to some boards adds a row to a feature-by-device matrix that the docs, the web UI, the issue tracker and every later contribution have to respect. That matrix grows with each contribution until nobody can tell what runs where, and users cannot answer "does my SmallTV do X" without reading the build flags.

A board-specific limit is accepted only where the hardware cannot support the feature, for example a chip without the flash or RAM for it. The pull request has to show that limit with measured numbers. Porting effort is not a hardware limit. WireGuard is the existing example: the ESP8266 cannot run it, as documented in the README.

## Building and testing

- Build every target listed above: `pio run -e smalltv -e smalltv_c2 -e smalltv_esp32 -e smalltv_esp32_wg -e smalltv_esp32_8mb`
- Report the flash and static RAM usage of each image the change affects
- State which boards the change ran on and which were only built. An untested board is fine, an unstated one is not
- On the ESP8266, report free heap while the feature runs alongside the stock ticker's TLS fetch
- CI fails any image left with less than 8 KB free in its app slot

## ESP8266 memory and untrusted input

The ESP8266 runs a theme with about 6.5 KB of heap free, and a failed allocation reboots the device. Theme packages, settings imports and HTTP requests come from outside, so code that reads them must stay within that budget whatever the input.

- No single heap block grows with the input. Store data per item in small blocks, as `BlockList` does for layers, or cap the total and reserve it exactly
- Constant tables and long strings go in flash: `PROGMEM` or `F()` on the ESP8266
- A loop over input-sized data, such as validating every image of a package, reads in chunks and calls `yield()`, so a large or crafted input cannot trip the watchdog. Report the time it takes on the largest input the format allows
- Every web handler starts with `requireAuth()`, which runs the cross-site and Host checks before the password check. An upload callback runs before its handler, so it asks `guardAllowed()` and the password at the start of the upload, and writes nothing to flash when either says no

## Code

- Match the layout and comment density of the surrounding code
- Keep unrelated changes, such as a fix to settings storage, in their own pull request
- `src/webui.html` is the source of the web UI. After editing it, run `python tools/gzip_webui.py` and commit the regenerated `src/webui.h` in the same commit
- A new setting gets a default in `Settings::setDefaults()` and appears in both `settingsToJson()` and `settingsApplyJson()`, so it survives the backup and restore in the System tab
- A change to the theme format (manifest rules, limits, tokens, sources, the package or STI format, the renderer) updates `skills/smalltv-theme-clock/scripts/stheme.py` and its references in the same pull request. `tests/theme/test_skill_parity.py` checks the packer, validator and renderer against the firmware
- Do not change `FW_VERSION` in `src/config.h`. The maintainer bumps it at release time
- Never commit credentials, API keys, WiFi passwords or captures from a real device

## Documentation

- A new feature gets a page under `docs/src/content/docs/features/` and an entry in the sidebar in `docs/astro.config.mjs`
- A new feature gets a bullet in the README feature list that links to the published page on `https://giovi321.github.io/smalltv-mod/`, not to the Markdown source

## Commits

- Commit messages use `type(scope): summary`, for example `feat(ha): ...`, `fix(notify): ...`, `docs(readme): ...`, `ci: ...`

## License

Contributions are released under the WTFPL, the same license as the rest of the repo.
