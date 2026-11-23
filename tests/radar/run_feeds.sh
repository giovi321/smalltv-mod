#!/bin/sh
# Native tests use the real pinned libraries installed by PlatformIO.
# The client capacity test requires a 32-bit C++ toolchain (Linux multilib).
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
STREAM_INCLUDE=${STREAM_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/StreamUtils/src}
bin=$(mktemp /tmp/smalltv-radar-tests.XXXXXX)
trap 'rm -f "$bin"' EXIT
c++ -std=c++11 -Wall -Wextra -Werror -g -DSTREAMUTILS_STREAM_READBYTES_IS_VIRTUAL=1 -Itests/radar/shim -Isrc -I"$JSON_INCLUDE" -I"$STREAM_INCLUDE" tests/radar/test_stream.cpp -o "$bin"
"$bin"
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" tests/radar/test_allocator.cpp -o "$bin"
"$bin"
c++ -m32 -std=c++11 -Wall -Wextra -Werror -g -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 -DARDUINOJSON_ENABLE_ARDUINO_PRINT=0 -DSTREAMUTILS_STREAM_READBYTES_IS_VIRTUAL=1 -Itests/radar/shim -Isrc -I"$JSON_INCLUDE" -I"$STREAM_INCLUDE" tests/radar/test_client.cpp -o "$bin"
"$bin"
node tests/radar/test_status_ui.js
