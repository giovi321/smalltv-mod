#!/bin/sh
# The layer limit of a build (THEME_MAX_LAYERS, default 32), checked on the
# compiled engine, not on the source text:
#   1. No flag, and -DTHEME_MAX_LAYERS=32, give the same object file (the
#      default is 32, and the error text is built from the same number).
#   2. The object file says "at most N layers" for the limit it was built with,
#      and for no other.
#   3. A limit of 0 or 256 or more does not compile (a show rule keeps its layer
#      in one byte, so 255 is the most a layer index can name).
#   4. The stack frame of render() holds one 2-byte handle per layer: 8 more
#      layers cost 16 bytes (the compiler may round up by one 16-byte step).
#      The two frames are printed: the figure for the stack cost of the cap.
#   sh tests/theme/check_layer_limit.sh [raised limit]      (default 40)
set -eu
cd "$(dirname "$0")/../.."
RAISED=${1:-40}
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
out=/tmp/smalltv-layer-limit
mkdir -p "$out"
# Same flags as check_render_stack.sh: 32-bit, -O2, with the stack usage file.
build() {  # build <name> [flags...]
  name=$1
  shift
  rm -f "$out/$name.o" "$out/$name.su" "$out/$name.cpp.su"
  c++ -m32 -O2 -std=c++11 -fstack-usage "$@" -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
    -c src/features/theme/ThemeEngine.cpp -o "$out/$name.o"
}
frame() {  # frame <name>: the stack bytes of render()
  su=$(ls "$out/$1.su" "$out/$1.cpp.su" 2>/dev/null | head -1)
  [ -n "$su" ] || { echo "layer limit check: no .su file for $1"; exit 1; }
  grep -F "smalltv::render(" "$su" | head -1 | awk -F'\t' '{print $2}'
}

build default
build at32 -DTHEME_MAX_LAYERS=32
build raised "-DTHEME_MAX_LAYERS=$RAISED"

# 1. No flag is the same engine as the flag at 32.
cmp "$out/default.o" "$out/at32.o" || { echo "FAIL: -DTHEME_MAX_LAYERS=32 differs from no flag"; exit 1; }
echo "ok   no flag and 32 give the same object file"

# 2. The message names the limit in use, and only that one.
for spec in "default:32" "at32:32" "raised:$RAISED"; do
  name=${spec%:*}
  limit=${spec#*:}
  strings -a "$out/$name.o" | grep -qxF "expected an array of at most $limit layers" \
    || { echo "FAIL: $name.o does not say \"at most $limit layers\""; exit 1; }
  others=$(strings -a "$out/$name.o" | grep -c "expected an array of at most [0-9]* layers" || true)
  [ "$others" = 1 ] || { echo "FAIL: $name.o has $others layer-limit messages, expected 1"; exit 1; }
done
echo "ok   the message names the limit (32, and $RAISED when raised), once"

# 3. A limit that cannot work does not compile.
for bad in 0 256 1000; do
  if c++ -m32 -std=c++11 -fsyntax-only -DTHEME_MAX_LAYERS=$bad -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
       src/features/theme/ThemeEngine.cpp 2> "$out/bad.txt"; then
    echo "FAIL: THEME_MAX_LAYERS=$bad compiled"
    exit 1
  fi
  grep -q "THEME_MAX_LAYERS" "$out/bad.txt" || { echo "FAIL: THEME_MAX_LAYERS=$bad failed, but not on the limit:"; cat "$out/bad.txt"; exit 1; }
done
for good in 1 255; do
  c++ -m32 -std=c++11 -fsyntax-only -DTHEME_MAX_LAYERS=$good -Isrc/features/theme -I"$THEME_JSON_INCLUDE" \
    src/features/theme/ThemeEngine.cpp
done
echo "ok   0, 256 and 1000 do not compile; 1 and 255 do"

# 4. The stack cost of the raised limit: 2 bytes a layer, in render() and nowhere else.
f32=$(frame at32)
fraised=$(frame raised)
want=$(( (RAISED - 32) * 2 ))
echo "render() frame: $f32 B at 32 layers, $fraised B at $RAISED (handles: 2 B x layers; expected +$want B)"
# The compiler rounds a frame to its stack alignment, so allow one step of 16 B
# above the exact figure; a frame that does not grow at all (a fixed array) fails.
[ $((fraised - f32)) -ge "$want" ] && [ $((fraised - f32)) -le $((want + 16)) ] \
  || { echo "FAIL: render() grew by $((fraised - f32)) B, expected $want B"; exit 1; }
# No other function's frame moves with the limit.
for name in at32 raised; do
  su=$(ls "$out/$name.su" "$out/$name.cpp.su" 2>/dev/null | head -1)
  grep -vF "smalltv::render(" "$su" | awk -F'\t' '{print $1 "\t" $2}' | sed 's/^[^:]*:[0-9]*:[0-9]*://' | sort > "$out/$name.frames"
done
cmp "$out/at32.frames" "$out/raised.frames" || { echo "FAIL: another function's frame depends on the limit"; exit 1; }
echo "ok   only render() changes with the limit"
echo "layer limit check: ok"
