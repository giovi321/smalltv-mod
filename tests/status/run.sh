#!/bin/sh
# The /api/status reply: bytes, and the heap it takes. Native; ArduinoJson
# comes from the firmware build's libdeps, as for the other tests.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" tests/status/test_status.cpp -o /tmp/smalltv-status-tests
/tmp/smalltv-status-tests
# 32-bit, so allocation sizes match the ESP8266; the wraps let the test count
# ArduinoJson's malloc calls as well as operator new.
c++ -m32 -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" -Wl,--wrap=malloc,--wrap=free,--wrap=realloc tests/status/test_status_memory.cpp -o /tmp/smalltv-status-memory-tests
/tmp/smalltv-status-memory-tests
