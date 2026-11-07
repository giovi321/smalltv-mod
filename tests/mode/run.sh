#!/bin/sh
# Native tests for the "mode" key of a save: the settings parser keeps the
# stored mode when the text is empty or unknown, and the page leaves the mode
# out when its list does not offer the stored one. Built as tests/auth/run.sh
# is (stand-in core headers of the fuzz target; needs ArduinoJson and node).
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -Itests/fuzz/shim -Isrc -Isrc/features/theme -I"$JSON_INCLUDE" \
  tests/mode/test_settings.cpp src/Settings.cpp -lm -o /tmp/smalltv-mode-settings-tests
/tmp/smalltv-mode-settings-tests
node tests/mode/test_webui.js
echo "mode tests: all passed"
