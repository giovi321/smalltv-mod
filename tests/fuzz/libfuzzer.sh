#!/bin/sh
# Optional: the same targets under libFuzzer (needs clang with the fuzzer
# runtime; the default tests/fuzz/run.sh does not). One binary per target:
#   sh tests/fuzz/libfuzzer.sh package|manifest|meta|import|font [libFuzzer args...]
# e.g. sh tests/fuzz/libfuzzer.sh package -max_total_time=60 \
#        /tmp/corpus-package examples/themes
set -eu
cd "$(dirname "$0")/../.."
target=$1
shift
THEME_JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
OUT=${FUZZ_OUT:-/tmp/smalltv-fuzz}
upper=$(echo "$target" | tr '[:lower:]' '[:upper:]')
mkdir -p "$OUT"
clang++ -m32 -std=c++11 -O1 -g -fno-omit-frame-pointer \
  -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
  -Wno-unused-parameter -D_FILE_OFFSET_BITS=64 \
  -DFUZZ_LIBFUZZER -DFUZZ_TARGET_"$upper" \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -Itests/fuzz/shim -Isrc -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
  tests/fuzz/fuzz_parsers.cpp src/Settings.cpp \
  src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp \
  -o "$OUT/fuzz_$target"
"$OUT/fuzz_$target" "$@"
