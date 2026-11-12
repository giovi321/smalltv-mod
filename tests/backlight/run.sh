#!/bin/sh
# The backlight plan (native) and a check that the sources are wired to it.
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc tests/backlight/test_backlight.cpp -o /tmp/smalltv-backlight-tests
/tmp/smalltv-backlight-tests
c++ -std=c++11 -Wall -Wextra -Werror -g tests/backlight/test_wiring.cpp -o /tmp/smalltv-backlight-wiring
/tmp/smalltv-backlight-wiring
echo "backlight suite done"
