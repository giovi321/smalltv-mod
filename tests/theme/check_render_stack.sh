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
renderLimit=${1:-1000}
# Host frames now: render 912 B (GCC 12; main 1,632), fontRow 288 B (GCC 12) and
# 320 B (GCC 15). A coverage row of 240 instead of 120 makes fontRow 400 B
# (GCC 12), so its limit sits between: 360.
fontLimit=${2:-360}
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
