# ESP8266mDNS, copy with one change

This folder is a copy of the `ESP8266mDNS` library (LEAmDNS) from the
ESP8266 Arduino core **3.1.2** (`libraries/ESP8266mDNS`). It is MIT licensed;
every source file keeps its original license header.

PlatformIO looks in the project's `lib/` before it looks at the framework's
libraries, so this copy replaces the core's one for the ESP8266 builds.
`library.json` limits it to `espressif8266`; the ESP32 builds do not see it
(they use their own `ESPmDNS`).

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

`LEAmDNS_Stream.h` holds the loop as a small template without any Arduino
include, so a host test (`tests/mdns/`) can run it with fake records.

## Everything else

Identical to core 3.1.2. To compare, diff against
`framework-arduinoespressif8266/libraries/ESP8266mDNS` of that core version.
