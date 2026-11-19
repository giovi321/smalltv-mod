#!/bin/sh
# The mDNS library copy in lib/ESP8266mDNS, two changes to the core's code:
#  - a response is handled one record at a time (peak: one record alive), and
#    _parseResponse uses that loop;
#  - a known answer in a query is read whole only when _parseQuery uses it.
# Native; no Arduino or lwIP needed. The second test compiles the real text of
# some functions of LEAmDNS_Control.cpp against a small fake (see the test).
set -eu
cd "$(dirname "$0")/../.."
c++ -std=c++11 -Wall -Wextra -Werror -g -Ilib/ESP8266mDNS/src tests/mdns/test_answer_stream.cpp -o /tmp/smalltv-mdns-tests
/tmp/smalltv-mdns-tests

python3 tests/mdns/extract_functions.py lib/ESP8266mDNS/src/LEAmDNS_Control.cpp \
  /tmp/smalltv-mdns-known-answer-functions.inc \
  _udpReadDomainIs _knownPTRTargetIsOurs _readKnownAnswer _replyMaskForService
c++ -std=c++11 -Wall -Wextra -Werror -g \
  '-DKNOWN_ANSWER_FUNCTIONS="/tmp/smalltv-mdns-known-answer-functions.inc"' \
  tests/mdns/test_known_answers.cpp -o /tmp/smalltv-mdns-known-answers
/tmp/smalltv-mdns-known-answers
