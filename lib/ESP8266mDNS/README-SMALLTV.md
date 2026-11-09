# ESP8266mDNS, copy with one change

This folder is a copy of the `ESP8266mDNS` library (LEAmDNS) from the
ESP8266 Arduino core **3.1.2** (`libraries/ESP8266mDNS`). It is MIT licensed;
every source file keeps its original license header.

PlatformIO looks in the project's `lib/` before it looks at the framework's
libraries, so this copy replaces the core's one for the ESP8266 builds.
`library.json` says `espressif8266`, but PlatformIO's default
`lib_compat_mode = soft` checks only the framework (`arduino`), not the
platform, so that line alone does not keep the copy out of an ESP32 build. The
ESP32 envs in `platformio.ini` list it in `lib_ignore` (from `[esp32_base]`);
they use their own `ESPmDNS`. `tools/check_mdns_copy.py` fails in CI when an
env of another platform lacks that line.

## What changed, and why

Only `src/LEAmDNS_Control.cpp` (`MDNSResponder::_parseResponse`) and one new
header, `src/LEAmDNS_Stream.h`.

The original reads **every** answer record of an incoming response into a
list, handles the list, and frees it after the whole packet. Each record is one
`new` of 288 to about 540 bytes. A response is only parsed while the device
waits for answers to its own probes (about 0.75 s after `MDNS.begin` and after
every Wi-Fi reconnect) or to service queries. During that window one big
response from another device can need several KB at once. On a device with a
few KB of heap the `new` fails, and on this core a failed `new` resets the chip
(reason 254).

The copy handles each record right after it is read and frees it before the
next one. The peak is one record. This is done when no service query is
waiting. That is the case for smalltv-mod (it never queries). The probe
conflict check only needs to look at one record at a time.

If a service query is waiting, the original code runs unchanged: the query
answers are linked across records in several passes, so they need the whole
list.

One difference in behaviour. In the original, `_processAnswers()` saw all
records of a packet in one call. In the copy it is called once per record, and
a `false` from it stops the remaining records of that packet. That cannot
happen here. In `LEAmDNS_Control.cpp` the function returns `false` in these
places only: its list is null (line 875, never: it gets the address of a
record), or `_processPTRAnswer` (line 994), `_processSRVAnswer` (1074),
`_processTXTAnswer` (1146), `_processAAnswer` (1210) or `_processAAAAAnswer`
(1299) returns `false`. Each of those returns `false` only for a null record
pointer, which the loop never passes. `_processPTRAnswer` also assigns the
result of `addAnswer()` (line 1043), and `addAnswer()` fails only for a null
answer (`LEAmDNS_Structs.cpp`, line 2075), which the surrounding `if` rules
out. The streaming path runs only when no service query waits, and then those
functions find no query to update and return `true` straight away. What does
stop the loop is a failed read of a record (`_readRRAnswer`), as in the core.

`LEAmDNS_Stream.h` holds the loop as a small template without any Arduino
include, so a host test (`tests/mdns/`) can run it with fake records.

## Everything else

Identical to core 3.1.2. To compare, diff against
`framework-arduinoespressif8266/libraries/ESP8266mDNS` of that core version.

## CI check: the copy must match the pinned core

`UPSTREAM.json` records the platform pin (`espressif8266 @ 4.2.1`), the
framework package version (`3.30102.0`, which is core 3.1.2) and the SHA-256 of
every upstream file as the core ships it. `tools/check_mdns_copy.py` runs in CI
after the firmware build (the build downloads the package) and fails when:

- `platformio.ini` pins another espressif8266 platform, or none;
- an env of another platform (ESP32) does not list `ESP8266mDNS` in `lib_ignore`;
- a file of this copy other than `src/LEAmDNS_Control.cpp` differs from its
  recorded hash, is missing, or is not listed in `UPSTREAM.json`;
- the installed `framework-arduinoespressif8266` has another version, or any of
  its `libraries/ESP8266mDNS` files differs from the recorded hash
  (`LEAmDNS_Control.cpp` included: the record holds the original hash).

Line endings are ignored. When it fails after a platform bump: diff the new
core's `libraries/ESP8266mDNS` against this folder, re-apply the change to
`_parseResponse` (or drop the copy if the core fixed it), then update the
version and hashes in `UPSTREAM.json`. The check's own tests are
`tests/mdns/test_check_copy.py`.
