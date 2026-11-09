#!/bin/sh
# Native tests for the manual firmware upload on the ESP8266: the rule that
# says whether the core's update buffer fits into the largest free block
# (src/UpdateBuffer.h), the page showing the refusal text, and a check that
# WebPortal.cpp uses the rule before Update.begin(). Needs a C++ compiler and
# node; no libraries.
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc \
  tests/update/test_update_buffer.cpp -o /tmp/smalltv-update-buffer-tests
/tmp/smalltv-update-buffer-tests
node tests/update/test_webui.js
node tests/update/test_wiring.js
echo "update tests: all passed"
