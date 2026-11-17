#!/bin/sh
# Builds tests/fuzz/fuzz_parsers.cpp for 32-bit with ASan + UBSan and runs it.
# FUZZ_ITERS (or --iters) sets the mutants per target; the default 2000 takes
# a few seconds once built. See tests/fuzz/README.md.
set -eu
cd "$(dirname "$0")/../.."
THEME_JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
OUT=${FUZZ_OUT:-/tmp/smalltv-fuzz}
mkdir -p "$OUT"
# Settings.cpp is built as the ESP8266 target against the stand-in core headers
# in tests/fuzz/shim, so the real settings parser is what gets fuzzed. The four
# objects build side by side: ArduinoJson makes each one slow to compile.
FLAGS="-m32 -std=c++11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all
  -Wall -Wextra -Werror -Wno-unused-parameter -D_FILE_OFFSET_BITS=64
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
  -Itests/fuzz/shim -Itests/rain -Isrc -Isrc/features/theme -Isrc/features/rain -I$THEME_JSON_INCLUDE"
pids=""
for source in tests/fuzz/fuzz_parsers.cpp src/Settings.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp \
              tests/fuzz/fuzz_rain.cpp src/features/rain/RainPng.cpp; do
  extra=""
  case "$source" in *rain*|*Rain*) extra="-include tests/fuzz/shim/progmem.h" ;; esac
  # shellcheck disable=SC2086
  c++ $FLAGS $extra -c "$source" -o "$OUT/$(basename "$source" .cpp).o" &
  pids="$pids $!"
done
for pid in $pids; do wait "$pid"; done
c++ -m32 -fsanitize=address,undefined "$OUT"/fuzz_parsers.o "$OUT"/Settings.o "$OUT"/ThemePackage.o "$OUT"/ThemeEngine.o -o "$OUT/fuzz_parsers"
c++ -m32 -fsanitize=address,undefined "$OUT"/fuzz_rain.o "$OUT"/RainPng.o -o "$OUT/fuzz_rain"
"$OUT/fuzz_parsers" --crash-dir "$OUT" "$@"
# The rain radar's PNG decoder: its own binary, no ArduinoJson (fuzz_rain.cpp).
"$OUT/fuzz_rain" --crash-dir "$OUT" "$@"
