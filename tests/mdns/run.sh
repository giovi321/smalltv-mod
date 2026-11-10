#!/bin/sh
# The mDNS library copy in lib/ESP8266mDNS: a response is handled one record at
# a time (peak: one record alive), and _parseResponse uses that loop.
# Native; no Arduino or lwIP needed.
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Ilib/ESP8266mDNS/src tests/mdns/test_answer_stream.cpp -o /tmp/smalltv-mdns-tests
/tmp/smalltv-mdns-tests
