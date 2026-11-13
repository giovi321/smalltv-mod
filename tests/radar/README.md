# Radar feed tests

Run `sh tests/radar/run_feeds.sh` from a native Linux environment with `c++`,
32-bit multilib support, Node, and the pinned ArduinoJson 7.4.3 and StreamUtils
1.9.2 dependencies from a firmware build. `JSON_INCLUDE` and `STREAM_INCLUDE`
can point to those libraries in another checkout. The runner does not install
packages or build firmware. Windows validation used MSVC's `vcvars32.bat` with
`/std:c++14`; the client test requires 32-bit pointers and uses the firmware's
`ARDUINOJSON_USE_DOUBLE=0`, with default pool, slot, and integer settings.

- `test_stream.cpp` exercises the production input wrapper and actual pinned
  chunk decoder at every wire boundary, including extensions and trailers.
  It covers packet gaps, plain and chunked JSON reads, every truncated wire
  prefix, real EOF, and timeouts (266 deterministic cases).
- `test_allocator.cpp` checks exact allocation/reserve limits, alignment,
  overflow, malloc/realloc failure, original-pointer retention and accounting.
- `test_client.cpp` compiles the actual radar client with minimal network and
  heap stand-ins. Synthetic receiver input includes 60 aircraft with all seven
  plotted fields and unique callsigns/hex IDs. The 8 KB and 12 KB caps rejected
  that fixture in a 32-bit MSVC build; 16 KB accepted all 60 and plotted 24
  there, but GCC 32-bit needed 16,440 bytes at peak. The aligned allocation
  header is 16 bytes on Linux x86 versus 8 bytes on ESP8266 and MSVC x86;
  the measured ESP8266 ABI has 8-byte `max_align_t` alignment and 8-byte JSON
  slots with the default 128-slot pools. The portable ceiling is 20 KB,
  preserving alignment. The same GCC fixture with 80 aircraft needs 20,824
  bytes and is rejected gracefully. This ceiling does not guarantee available
  heap; the separate live-heap reserve still rejects allocations sooner.
  Separate oversized-array and 100 KB retained-string cases report `NoMemory`
  without replacing cached aircraft. Low-heap and subsequent-poll diagnostics,
  API/receiver array forms, and chunk decoding for every source are covered.
  Actual-client truncation before JSON completion rejects the poll and keeps
  cached aircraft; complete JSON missing terminal chunk framing is accepted
  under the existing streaming contract. Exhaustive wire-prefix tests apply
  to the separately drained decoder, not HTTP acceptance by the client.
- `test_status_ui.js` checks that cached aircraft do not hide the failed stage
  or parser/TLS errors. `tests/status/test_status.cpp` covers JSON output.

The 4 KB ESP8266 live-heap reserve is conservative, not a measured minimum;
a host test does not reproduce lwIP timing or prove a hardware reset fix.
The production JSON parser stops after the complete root value, so valid JSON
followed by missing HTTP chunk trailers is not separately rejected. Decoder
truncation tests explicitly distinguish an incomplete transfer from `ended()`.

## Reset baseline

Before this split was deployed, the official released 3.5.1 lean binary
(SHA-256 `516d780aa9ba4a4efa4a69c68875b754ff0e56c4617e1bbed380695668db5ee0`)
reproduced an Exception (`0x1d`, EPC `0x4000df64`, address `0`) after eight
HTTP 200 / parse-failed observations with the direct adsb.fi feed at 50 km.
This establishes that the signature predates these changes. It does not
identify the cause or prove the allocation guard fixes the reset. No device
captures, coordinates, or network addresses are included in these fixtures.
