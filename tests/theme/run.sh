#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
THEME_JSON_INCLUDE=.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_engine.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-tests
/tmp/smalltv-theme-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_builtin.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-builtin-tests
/tmp/smalltv-theme-builtin-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_show_time.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-show-time-tests
/tmp/smalltv-theme-show-time-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_text_tokens.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-text-tokens-tests
/tmp/smalltv-theme-text-tokens-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_frame.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-frame-tests
/tmp/smalltv-theme-frame-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -pthread -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_data.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-data-tests
/tmp/smalltv-theme-data-tests
# The allocator that holds a data reply's parse to a ceiling.
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_budget_allocator.cpp -o /tmp/smalltv-budget-allocator-tests
/tmp/smalltv-budget-allocator-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_package.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-package-tests
/tmp/smalltv-theme-package-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_font.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-font-tests
/tmp/smalltv-theme-font-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_font_review.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-font-review-tests
/tmp/smalltv-theme-font-review-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_font_review_example.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-font-review-example
/tmp/smalltv-theme-font-review-example
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_render.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-render-tests
/tmp/smalltv-theme-render-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -DEXPECT_DOUBLE_JSON=1 -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_validation.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-validation-tests
# Image rows blended in the reader, so render() holds no colour/alpha buffers.
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_blend_row.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-blend-row-tests
/tmp/smalltv-theme-blend-row-tests
sh tests/theme/check_render_stack.sh
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_language.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-language-tests
/tmp/smalltv-theme-language-tests
/tmp/smalltv-theme-validation-tests
# The same tests with floats, as the device parses numbers (ARDUINOJSON_USE_DOUBLE=0).
# EXPECT_FLOAT_JSON makes the build fail if that flag is missing.
c++ -std=c++11 -Wall -Wextra -Werror -g -DARDUINOJSON_USE_DOUBLE=0 -DEXPECT_FLOAT_JSON=1 -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_validation.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-validation-float-tests
/tmp/smalltv-theme-validation-float-tests
# The layer limit raised to 40 (THEME_MAX_LAYERS): exactly 40 layers load and 41
# are refused with the limit named; a 40-layer theme renders; the object-file and
# stack checks in check_layer_limit.sh. EXPECTED_MAX_LAYERS says what the test
# build must have, so a flag that does not reach the header fails the test.
c++ -std=c++11 -Wall -Wextra -Werror -g -DTHEME_MAX_LAYERS=40 -DEXPECTED_MAX_LAYERS=40 -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_validation.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-validation-40-tests
/tmp/smalltv-theme-validation-40-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -DTHEME_MAX_LAYERS=40 -DEXPECTED_MAX_LAYERS=40 -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_render.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-render-40-tests
/tmp/smalltv-theme-render-40-tests
sh tests/theme/check_layer_limit.sh 40
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" -I".pio/libdeps/smalltv_esp32_8mb/GFX Library for Arduino/src" tests/theme/check_example.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-example-check
/tmp/smalltv-theme-example-check examples/themes/pixel-room.stheme
/tmp/smalltv-theme-example-check examples/themes/terminal-ops.stheme
/tmp/smalltv-theme-example-check examples/themes/bebas-clock.stheme
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" tests/theme/test_catalog.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-catalog-tests
/tmp/smalltv-theme-catalog-tests
# The memory margin after a load: the pure check and the wiring of the device code.
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme tests/theme/test_margin.cpp -o /tmp/smalltv-margin-tests
/tmp/smalltv-margin-tests
# 32-bit, so allocation sizes match the ESP8266; the wraps let the test count
# ArduinoJson's malloc calls as well as operator new.
c++ -m32 -std=c++11 -Wall -Wextra -Werror -g -Isrc/features/theme -I"$THEME_JSON_INCLUDE" -Wl,--wrap=malloc,--wrap=free,--wrap=realloc tests/theme/test_memory.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-memory-tests
/tmp/smalltv-theme-memory-tests
# The same at the raised layer limit: the largest block stays in its budget, what
# one more layer costs stays in its bound, and the load peak is printed.
c++ -m32 -std=c++11 -Wall -Wextra -Werror -g -DTHEME_MAX_LAYERS=40 -DEXPECTED_MAX_LAYERS=40 -Isrc/features/theme -I"$THEME_JSON_INCLUDE" -Wl,--wrap=malloc,--wrap=free,--wrap=realloc tests/theme/test_memory.cpp src/features/theme/ThemePackage.cpp src/features/theme/ThemeEngine.cpp -o /tmp/smalltv-theme-memory-40-tests
/tmp/smalltv-theme-memory-40-tests
# The Wi-Fi reserve: the test packages that crashed (32 x 65) and that first
# held under 5 KB (32 x 20) must leave less than the reserve, so they are refused.
python3 tools/make_oom_theme.py /tmp/smalltv-oom-32x65.stheme 32 65
python3 tools/make_oom_theme.py /tmp/smalltv-oom-32x20.stheme 32 20
/tmp/smalltv-theme-memory-tests --below-reserve /tmp/smalltv-oom-32x65.stheme /tmp/smalltv-oom-32x20.stheme
