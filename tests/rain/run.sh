#!/bin/sh
# Host tests of the rain radar: the frame set (RainClient.cpp and Settings.cpp
# compiled as they are, against an in-memory LittleFS and a scripted
# RainViewer), then the PNG decoder against a zlib reference.
#   sh tests/rain/run.sh    (ArduinoJson from a PlatformIO build, or set THEME_JSON_INCLUDE)
set -eu
cd "$(dirname "$0")/../.."
THEME_JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
OUT=${TMPDIR:-/tmp}/smalltv-rain-tests
mkdir -p "$OUT"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 \
  -include tests/rain/shim/rain_host.h \
  -Itests/rain/shim -Itests/fuzz/shim -Isrc -Isrc/features/rain -I"$THEME_JSON_INCLUDE" \
  tests/rain/test_frameset.cpp src/features/rain/RainClient.cpp src/Settings.cpp \
  -o "$OUT/test_frameset"
"$OUT/test_frameset"
echo "rain frame set tests: all passed"

# Static RAM (PR #122): rain off must cost next to nothing. The client's
# state (RainState) and the decoder's work area live on the heap or the
# stack while they are used; what stays static is pointers and a byte.
# Built 32-bit like the ESP8266: no static object over 8 bytes.
g++ -m32 -std=c++17 -O1 -c -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 \
  -include tests/rain/shim/rain_host.h -Itests/rain/shim -Itests/fuzz/shim -Isrc -Isrc/features/rain \
  -I"$THEME_JSON_INCLUDE" src/features/rain/RainClient.cpp -o "$OUT/RainClient32.o"
g++ -m32 -std=c++17 -O1 -c -Dmemcpy_P=memcpy -Itools/rain_fwtest -Isrc/features/rain \
  src/features/rain/RainPng.cpp -o "$OUT/RainPng32.o"
for o in RainClient32 RainPng32; do
  big=$(nm -S -C "$OUT/$o.o" | while read -r addr size type name; do
    case "$type" in b|B|d|D) [ $((0x$size)) -gt 8 ] && echo "$name ($((0x$size)) B)" ;; esac
  done || true)
  if [ -n "$big" ]; then
    echo "FAIL $o: static objects over 8 bytes: $big"
    exit 1
  fi
done
echo "rain static RAM: no static object over 8 bytes (RainClient.cpp, RainPng.cpp)"

# A theme upload drops the rain frames first (ThemeWeb.cpp, read as source).
python3 tests/rain/check_theme_wiring.py

# The colour lookup: the flash table (ESP8266) and the rule computed on the
# ESP32 images must agree on every RGB565 value.
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
g++ -std=c++17 -O2 $SAN -Dmemcpy_P=memcpy -DRAIN_CLUT_IN_FLASH=0 -Itools/rain_fwtest -Isrc/features/rain \
  -c src/features/rain/RainPng.cpp -o "$OUT/RainPng_ram.o"
g++ -std=c++17 -O2 $SAN -Dmemcpy_P=memcpy -Itools/rain_fwtest -Isrc/features/rain \
  tests/rain/test_clut.cpp "$OUT/RainPng_ram.o" -o "$OUT/test_clut"
"$OUT/test_clut"

# The PNG decoder against a zlib reference (tools/rain_fwtest/groundtruth.py:
# zlib + premultiplication + the same crop, upscale and colour lookup), on
# the synthetic tiles of the fuzz corpus, at the shortest, default and
# longest ranges, with both forms of the colour lookup. Tiles in the colours
# of RainViewer's scheme (every real tile) must match to the pixel: the LZ
# window keeps each pixel's index in that scheme, so copies rebuild exact
# bytes. Before that, and before the fix of copies whose source is a row's
# filter byte, such tiles were off by 17-45 %.
for mode in 1 0; do
  g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter $SAN \
    -Dmemcpy_P=memcpy -DRAIN_CLUT_IN_FLASH=$mode -Itools/rain_fwtest -Itests/rain -Isrc/features/rain \
    tools/rain_fwtest/test.cpp src/features/rain/RainPng.cpp -o "$OUT/rain_decode$mode"
done
fail=0
for tile in tests/fuzz/corpus/rain/*.png; do
  # The off-scheme tile goes through the nearest-colour fallback.
  case "$(basename "$tile")" in offscheme-*) limit=15.0 ;; *) limit=0.0 ;; esac
  for k in 24 120 240; do
    "$OUT/rain_decode1" "$tile" "$OUT/rain_decode1.raw" "$k" >/dev/null
    "$OUT/rain_decode0" "$tile" "$OUT/rain_decode0.raw" "$k" >/dev/null
    if ! cmp -s "$OUT/rain_decode1.raw" "$OUT/rain_decode0.raw"; then
      echo "FAIL decoder: $(basename "$tile") at crop $k: flash and computed lookups disagree"
      fail=1
    fi
    # The small-memory mode (2 KB ring + history segments) must give the
    # same bytes as the full 8 KB window.
    if ! "$OUT/rain_decode1" "$tile" "$OUT/rain_ring.raw" "$k" ring >/dev/null ||
       ! cmp -s "$OUT/rain_decode1.raw" "$OUT/rain_ring.raw"; then
      echo "FAIL decoder: $(basename "$tile") at crop $k: the small-memory mode differs from the full window"
      fail=1
    fi
    pct=$(cd tools/rain_fwtest && python3 groundtruth.py "../../$tile" "$OUT/rain_decode1.raw" "$k" |
          sed -E 's/.*channel: [0-9]+ \(([0-9.]+)%\).*/\1/')
    if ! python3 -c "import sys; sys.exit(0 if float('$pct') <= $limit else 1)"; then
      echo "FAIL decoder: $(basename "$tile") at crop $k: $pct % of pixels off by more than 12 (limit $limit %)"
      fail=1
    fi
  done
done
# A full filesystem: the decoder checks every write and fails on a short
# one (never a truncated frame passed as decoded), with both window forms.
tile=tests/fuzz/corpus/rain/dynamic.png
for lim in 0 239 10000 57599; do
  for w in "" ring; do
    if out=$(RAIN_WRITE_LIMIT=$lim "$OUT/rain_decode1" "$tile" "$OUT/rain_short.raw" 120 $w) ||
       ! echo "$out" | grep -q "short write"; then
      echo "FAIL decoder: a short write at $lim bytes ($w) was not reported"
      fail=1
    fi
  done
done
# The heap floor (RAIN_DECODE_MIN_HEAP): free heap under it right after the
# decoder's buffers are allocated, or at any later output row, stops the
# decode, reported as such (the caller then skips the tile).
for at in 0 1 120 239; do
  for w in "" ring; do
    if out=$(RAIN_HEAP_LOW=$at "$OUT/rain_decode1" "$tile" "$OUT/rain_low.raw" 120 $w) ||
       ! echo "$out" | grep -q "low heap"; then
      echo "FAIL decoder: heap floor crossed at check $at ($w) was not reported"
      fail=1
    fi
  done
done
# A tile of ~77,000 short copies that alternate between two stored history
# segments (second review of PR #122): in the small-memory mode every copy
# reads another segment, a LittleFS reopen on the device. With 5 ms per read
# (virtual time) the 60 s budget must stop it within ~12,000 reads, reported
# as such; with the 8 KB window (no history) it decodes, a clear sky.
# The seeds, at the same cost, stay well within the budget.
python3 tests/rain/make_slow_tile.py tests/fuzz/corpus/rain/dynamic.png "$OUT/alternate.png" --alternate >/dev/null
"$OUT/rain_decode1" "$OUT/alternate.png" "$OUT/rain_alt.raw" 120 >/dev/null ||
  { echo "FAIL decoder: the alternating tile did not decode with the full window"; fail=1; }
if out=$(RAIN_GET_MS=5 "$OUT/rain_decode1" "$OUT/alternate.png" "$OUT/rain_alt.raw" 120 ring) ||
   ! echo "$out" | grep -q "too slow"; then
  echo "FAIL decoder: the alternating tile ran past the time budget unreported"; fail=1
fi
reads=$(echo "$out" | sed -nE 's/.*segments stored, ([0-9]+) reads.*/\1/p')
[ "${reads:-99999}" -le 12001 ] || { echo "FAIL decoder: the time budget stopped the alternating tile after ${reads:-?} reads"; fail=1; }
for tile in tests/fuzz/corpus/rain/*.png; do
  case "$tile" in *offscheme*) continue;; esac
  RAIN_GET_MS=5 "$OUT/rain_decode1" "$tile" "$OUT/rain_budget.raw" 120 ring >/dev/null ||
    { echo "FAIL decoder: $(basename "$tile") ran out of the time budget at 5 ms per read"; fail=1; }
done
# A hostile tile (PR #122 review): ~200,000 empty fixed blocks, or ~21,000
# empty dynamic ones, before the pixel data, up to the 256 KB download cap.
# Same frame as the plain tile, decoded quickly (the Huffman tables cost
# O(n) to build and the fixed ones are built once per decode: before, the
# fixed-block tile took ~2 s on a PC, minutes on an ESP8266), with yield()
# called all along.
for kind in fixed dynamic; do
  python3 tests/rain/make_slow_tile.py tests/fuzz/corpus/rain/dynamic.png "$OUT/slow-$kind.png" \
    $( [ $kind = dynamic ] && echo --dynamic ) >/dev/null
  for w in "" ring; do
    start=$(date +%s%N)
    out=$("$OUT/rain_decode1" "$OUT/slow-$kind.png" "$OUT/rain_slow.raw" 120 $w) || { echo "FAIL decoder: slow-$kind ($w) did not decode"; fail=1; continue; }
    ms=$(( ($(date +%s%N) - start) / 1000000 ))
    "$OUT/rain_decode1" tests/fuzz/corpus/rain/dynamic.png "$OUT/rain_plain.raw" 120 >/dev/null
    cmp -s "$OUT/rain_slow.raw" "$OUT/rain_plain.raw" || { echo "FAIL decoder: slow-$kind ($w) gives another frame"; fail=1; }
    [ "$ms" -lt 3000 ] || { echo "FAIL decoder: slow-$kind ($w) took $ms ms"; fail=1; }
    y=$(echo "$out" | sed -nE 's/.*, ([0-9]+) yields.*/\1/p')
    [ "${y:-0}" -ge 40 ] || { echo "FAIL decoder: slow-$kind ($w) yielded ${y:-0} times"; fail=1; }
  done
done
[ "$fail" = 0 ] || exit 1
echo "rain decoder vs zlib reference: all passed"
