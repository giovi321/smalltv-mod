# Parser fuzzing

Packages, theme manifests and imported settings arrive from outside the device,
so they are treated as untrusted input. This harness feeds mutated copies of the
repository's own examples to those parsers, built for 32 bits (like the ESP8266)
with AddressSanitizer and UndefinedBehaviorSanitizer. The first memory error or
undefined behaviour aborts the run.

| Target | Entry point | Seeds |
|---|---|---|
| `package` | `Package::load()`, `row()`, `resolve()` (STH1 container, STI1 and STI2 images), then the engine and `render()` on whatever loads; `inspectInstalledTheme()` | `examples/themes/*.stheme`, a small built-in package, each STI2 file in `tests/theme/sti/` in a spec 2 package |
| `manifest` | `parseTheme()` on a string and through the streaming `ManifestSource`, then the engine | `examples/themes/*/theme.json` |
| `meta` | `readThemeMeta()` (the `<id>.meta` sidecar) | `corpus/meta/` |
| `import` | `deserializeJson()` + `settingsApplyJson()` as in `handleImport()`, then `settingsToJson()` and a second apply | `corpus/import/`, the device's own default export |

`src/Settings.cpp` is compiled as it is, as the ESP8266 target, against the
stand-in Arduino/ESP8266 headers in `shim/`. Nothing under `src/` is changed or
copied.

## Running

```
sh tests/fuzz/run.sh                    # 2000 mutants per target, seed 1
sh tests/fuzz/run.sh --iters 100000 --seed 7
sh tests/fuzz/run.sh --target import    # one target
```

It needs a 32-bit C++ toolchain (`g++-multilib`) and ArduinoJson; the include
path defaults to the one PlatformIO fetches, or set `THEME_JSON_INCLUDE`.

The mutants come from a fixed-seed generator: byte flips, truncation, extreme
integers written into any field, container and image headers whose lengths
disagree with the data, numbers in JSON replaced by 0, negative and huge values,
values swapped for other types, very long strings and deep nesting. For
packages the manifest and image fields are changed inside a container whose
lengths are rebuilt, so mutants get past the index checks and into the parsers.
The same seed and count always run the same cases.

When a case fails, the sanitizer report is printed and the input is saved as
`crash-<target>-<n>` next to the binary (`$FUZZ_OUT`, default
`/tmp/smalltv-fuzz`). Replay it with:

```
/tmp/smalltv-fuzz/fuzz_parsers --target package --repro /tmp/smalltv-fuzz/crash-package-1234
```

## libFuzzer

Optional, and not used by CI: `sh tests/fuzz/libfuzzer.sh <target> [libFuzzer args]`
builds one target with `clang -fsanitize=fuzzer,address,undefined`. Point it at
the example directories to start from the same seeds.
