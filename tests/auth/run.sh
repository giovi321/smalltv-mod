#!/bin/sh
# Native tests for the web password: the stored-settings and import rules
# (Settings.cpp, built against the stand-in core headers of the fuzz target;
# needs ArduinoJson, as tests/sun/run.sh does), the password card of the page
# and a check that every login decision looks at the password alone.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -Itests/fuzz/shim -Isrc -Isrc/features/theme -I"$JSON_INCLUDE" \
  tests/auth/test_settings.cpp src/Settings.cpp -lm -o /tmp/smalltv-auth-settings-tests
/tmp/smalltv-auth-settings-tests
node tests/auth/test_webui.js
node tests/auth/test_wiring.js
echo "auth tests: all passed"
