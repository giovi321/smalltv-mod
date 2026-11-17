#!/bin/sh
# render() runs on the ESP8266's 4 KB loop stack, under the whole drawing
# path (TRAPS T12). Its frame was 1,584 B on the device, 1,200 B of it three
# row buffers; the colour and alpha rows now live in the image reader, and a
# package font's coverage row in fontRow(), off the image path. This holds
# the frames to limits on the host build (32-bit, -O2, -fstack-usage), so a
# row buffer put back into render() fails here, not on the device.
#   sh tests/theme/check_render_stack.sh [render limit] [fontRow limit]   (bytes)
set -eu
cd "$(dirname "$0")/../.."
# The limits are for what this check must catch: a row buffer (the 720 B
# colour and alpha rows, or one of them) coming back. The frame sizes depend on
# the compiler, so each limit sits far above what any compiler gave so far, and
# well below what the regression gives:
#   render:  912 B (GCC 12), 864 B (GCC 15); with the 720 B back, 1,632 B (main)
#   fontRow: 288 B (GCC 12), 320 B (GCC 15); with 720 B added, over 1,000 B
# A compiler change moves these by tens of bytes, so a limit 200-300 B above
# them does not trip on one, and a 720 B buffer still fails by a wide margin.
renderLimit=${1:-1200}
fontLimit=${2:-520}
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
out=/tmp/smalltv-render-stack
mkdir -p "$out"
rm -f "$out"/ThemeEngine.o "$out"/ThemeEngine.su
c++ -m32 -O2 -std=c++11 -fstack-usage -Isrc/features/theme -I"$THEME_JSON_INCLUDE" -c src/features/theme/ThemeEngine.cpp -o "$out/ThemeEngine.o"
su=$(ls "$out"/ThemeEngine.su "$out"/ThemeEngine.cpp.su 2>/dev/null | head -1)
[ -n "$su" ] || { echo "render stack check: no .su file"; exit 1; }
bad=0
for spec in "smalltv::render(=$renderLimit" "smalltv::fontRow(=$fontLimit"; do
  name=${spec%=*}
  limit=${spec##*=}
  line=$(grep -F "$name" "$su" | head -1)
  [ -n "$line" ] || { echo "render stack check: $name not in $su"; exit 1; }
  size=$(printf '%s\n' "$line" | awk -F'\t' '{print $2}')
  if [ "$size" -gt "$limit" ]; then
    echo "OVER $name): $size B (limit $limit)"
    bad=1
  else
    echo "ok   $name): $size B (limit $limit)"
  fi
done
if [ "$bad" != 0 ]; then
  echo "render stack check: FAILED, a frame is over its limit"
  exit 1
fi
echo "render stack check: ok"
