#!/bin/sh
# Build options for forks (src/BuildInfo.h): the version suffix and "no GitHub
# self-update". Native; ArduinoJson comes from the firmware build's libdeps,
# as for the other tests.
#  - test_build_info.cpp: the label and the switch, built with no flag and
#    with both flags.
#  - test_ota_off.cpp: src/OtaUpdate.cpp with the update off answers "no", and
#    its object file holds none of the download code (no HTTP or update
#    library, none of the download texts).
#  - test_webui.js: the update card on the page.
#  - test_wiring.js: the web handlers, the boot sequence, which places show the
#    label and which keep the plain release number.
#  - test_env.py: platformio.ini, the CI workflow, and the image check.
# tests/status/run.sh covers the "githubUpdate" key of the status reply.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
OUT=/tmp/smalltv-buildflags
mkdir -p "$OUT"

c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" \
  tests/buildflags/test_build_info.cpp -o "$OUT/info-plain"
"$OUT/info-plain"
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" \
  -DEXPECT_FORK '-DFW_VERSION_SUFFIX="-fork"' -DFW_NO_GITHUB_UPDATE \
  tests/buildflags/test_build_info.cpp -o "$OUT/info-fork"
"$OUT/info-fork"

# OtaUpdate.cpp on the host with the fuzz target's stand-in headers.
OTA_FLAGS="-std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g -DSMALLTV_ESP8266 -DARDUINOJSON_USE_DOUBLE=0 \
  -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DFW_NO_GITHUB_UPDATE -Itests/fuzz/shim -Isrc -Isrc/features/theme -I$JSON_INCLUDE"
c++ $OTA_FLAGS -c src/OtaUpdate.cpp -o "$OUT/ota-off.o"
c++ $OTA_FLAGS -c tests/buildflags/test_ota_off.cpp -o "$OUT/test-ota-off.o"
c++ "$OUT/ota-off.o" "$OUT/test-ota-off.o" -o "$OUT/ota-off"
"$OUT/ota-off"
# The object file: no download library symbol, none of the download texts.
# The texts are the ones check_images.py looks for in the firmware image.
if nm -C "$OUT/ota-off.o" | grep -Ei 'HTTPClient|httpUpdate|BearSSL|WiFiClientSecure|probeMfln|verNum'; then
  echo "FAIL: the update-off object file still holds download code" >&2
  exit 1
fi
for text in "server reported no update" "not enough heap even at boot" "download failed"; do
  if strings -a "$OUT/ota-off.o" | grep -F "$text"; then
    echo "FAIL: the update-off object file holds the text: $text" >&2
    exit 1
  fi
  # ... and the text is still in the source, so this check can fail.
  grep -qF "$text" src/OtaUpdate.cpp
done

node tests/buildflags/test_webui.js
node tests/buildflags/test_wiring.js
python3 -I -m unittest discover -s tests/buildflags -p 'test_env.py'
echo "buildflags tests: all passed"
