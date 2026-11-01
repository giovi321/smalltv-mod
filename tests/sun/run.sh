#!/bin/sh
# Native tests for the sun-dimming logic (src/SunTimes.cpp). Needs no libraries.
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -Itests/sun tests/sun/test_sun.cpp src/SunTimes.cpp -lm -o /tmp/smalltv-sun-tests
/tmp/smalltv-sun-tests
