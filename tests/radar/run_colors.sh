#!/bin/sh
# Native radar settings and page tests; uses the fuzz target core headers.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -Itests/fuzz/shim -Isrc -Isrc/features/theme -I"$JSON_INCLUDE" \
  tests/radar/test_color_settings.cpp src/Settings.cpp -lm -o /tmp/smalltv-radar-settings-tests
/tmp/smalltv-radar-settings-tests
node tests/radar/test_colors_webui.js
echo "radar color tests: all passed"
