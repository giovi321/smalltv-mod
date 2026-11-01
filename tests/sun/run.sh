#!/bin/sh
# Native tests for the sun-dimming logic (src/SunTimes.cpp) and its settings.
# The first test needs no libraries. The settings test builds Settings.cpp
# against the stand-in core headers of the fuzz target and needs ArduinoJson
# (as tests/theme/run.sh does).
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -Itests/sun tests/sun/test_sun.cpp src/SunTimes.cpp -lm -o /tmp/smalltv-sun-tests
/tmp/smalltv-sun-tests
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -Itests/fuzz/shim -Isrc -Isrc/features/theme -I"$JSON_INCLUDE" \
  tests/sun/test_settings.cpp src/Settings.cpp -lm -o /tmp/smalltv-sun-settings-tests
/tmp/smalltv-sun-settings-tests
node tests/sun/test_webui.js
