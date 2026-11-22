#!/bin/sh
# Arduino_GFX's YCbCr tables in flash on the ESP8266 (tools/patch_gfx_ycbcr.py):
#  - the patch script: patches the pinned files once, refuses any other version;
#  - the patched header, built as on the ESP8266 with a host pgm_read_word:
#    every table value and every pixel of writeYCbCrPixels read the same.
# Needs a firmware build of smalltv first (the patched library in .pio/libdeps).
set -eu
cd "$(dirname "$0")/../.."
python3 -m unittest discover -s tests/gfx -p 'test_patch_ycbcr.py'
c++ -std=c++11 -Wall -Wextra -Werror -O1 -DESP8266 -Itests/gfx/stub \
  "-I.pio/libdeps/smalltv/GFX Library for Arduino/src" \
  tests/gfx/test_ycbcr_read.cpp -o /tmp/smalltv-gfx-ycbcr
/tmp/smalltv-gfx-ycbcr
