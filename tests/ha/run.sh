#!/bin/sh
# Native tests for the boot-time allocation of the Home Assistant screen store
# and MQTT client (src/features/ha/HaBoot.cpp, HaScreens.cpp, src/MqttClient.cpp).
# Needs ArduinoJson (from the firmware build's libdeps, as for the other tests).
# The page test and the wiring test are plain node scripts, run beside this one
# in CI (build.yml): node tests/ha/test_webui_ha.js, node tests/ha/test_wiring.js.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
# Settings.cpp and the three HA sources are built against the stand-in core
# headers of the fuzz target (tests/fuzz/shim) plus tests/ha/shim.
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g \
  -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
  -include tests/ha/host_core.h \
  -Itests/ha/shim -Itests/fuzz/shim -Isrc -Isrc/features/ha -Isrc/features/theme -I"$JSON_INCLUDE" \
  tests/ha/test_ha_alloc.cpp src/features/ha/HaBoot.cpp src/features/ha/HaScreens.cpp \
  src/MqttClient.cpp src/Settings.cpp -lm -o /tmp/smalltv-ha-alloc-tests
/tmp/smalltv-ha-alloc-tests
