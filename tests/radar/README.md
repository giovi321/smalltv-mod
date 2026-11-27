# Radar feed tests

Run `sh tests/radar/run_feeds.sh` on native Linux with `c++`, 32-bit multilib,
Node, and pinned ArduinoJson 7.4.3 / StreamUtils 1.9.2 headers. `JSON_INCLUDE`
and `STREAM_INCLUDE` can select libraries from another checkout. The runner
installs no packages and makes no network requests.

- `test_stream.cpp` uses the actual StreamUtils decoder for 270 deterministic
  cases: split chunks, packet gaps, extensions/trailers, truncation, slow-body
  and scalar header deadlines, and millis wraparound.
- `test_allocator.cpp` checks allocation/reallocation accounting, alignment,
  overflow, allocation failure and the total heap reserve and contiguous allocation fit.
- `fixtures/aircraft.json` is a small synthetic readsb/dump1090 receiver sample.
- `test_client.cpp` compiles the actual client with deterministic network and
  heap stand-ins, including radar/carousel storage release and reacquisition,
  direct-feed MFLN backoff/re-probing, and local-receiver bypass.
  Both `ac` and `aircraft` feeds contain 100, 500 and 5,000
  background aircraft plus a nearer aircraft arriving last. Metadata before
  and after the list, numeric metadata lookahead, decoded top-level keys, nested
  and string decoys, NUL-containing keys, both-array precedence and empty or
  altitude-filtered lists are covered. Every incomplete prefix of an envelope,
  malformed separators, oversized retained values, excessive nesting and low
  or fragmented heap must preserve cached aircraft and the last success time.
  All source selections also run through real chunk decoding with packet gaps.
- `test_status_ui.js` and `tests/status/test_status.cpp` check parser/HTTP status
  formatting, escaped streamed JSON, and omission of the removed TLS diagnostic.

## Transport dependency and limits

StreamUtils 1.9.2 supplies `ChunkDecodingStream`, which removes HTTP/1.1 chunk
framing before the JSON parser reads the body. The timed input wrapper handles
packet gaps beneath the decoder; StreamUtils is not an aircraft-data parser.

ESP8266 direct-feed MFLN probes try 512 and 1024 bytes, then select a 4096-byte
fallback buffer. Either provider can send larger TLS records, and smaller
records can still leave insufficient ordinary heap for packets or parsing.
HTTPS webhooks currently share the adsb.fi probe/cache key, rather than probing
their own URL's host. A plain HTTP LAN URL bypasses TLS, not parser allocation
checks. The 16000-byte TLS largest-block floor is a conservative preflight,
not a measured handshake allocation size or a guarantee against resets. Direct
ESP8266 feeds without MFLN are not polled; upstream re-probes after an hour
or a settings save.

## Memory and parsing contract

There is no aircraft-count-dependent document limit. A 4 KB retained temporary
allocation budget includes the nearest-24 staging buffer, field filter, and one
record/key document and allocator headers; it is reused across the entire
feed on both ESP families. TLS buffers, packet queues and call-stack memory
are outside this budget. Internal realloc may briefly hold both old and
replacement blocks; the ESP8266 reserve check allows for the full replacement.
An oversized retained field or key can still report `NoMemory`. On ESP8266,
allocation leaves `WifiReserveFreeHeap` (5,120 B) in total free heap. The largest
block must fit the allocation, including allocator metadata. There is no
additional largest-block reserve. These checks are not proof against every
reset or asynchronous SDK allocation failure.

Only top-level decoded `ac` / `aircraft` keys select arrays. An array-valued
`ac` takes precedence regardless of key order; a non-array `ac` permits the
`aircraft` fallback. Duplicate occurrences of either key are
rejected. Each selected array element must be an object. Other metadata uses
ArduinoJson's existing filtered skip behavior, including that library's JSON
extensions; this is not a new strict RFC JSON validator. Results are published
only after a complete root object, including metadata after the aircraft list.

As before, parsing stops at the root closing brace. Complete JSON lacking the
terminal HTTP chunk framing, or with later bytes left unread, remains accepted; the exhaustive wire truncation
checks apply to the separately drained decoder, while incomplete JSON fails in
the real client and preserves its cache. The shared `WaitingStream` handles
packet gaps; a deadline client limits headers, redirects and body reads to one
`httpTimeout` budget, including time already spent opening the request. Blocking
SDK DNS/connect/TLS calls cannot be preempted and may return after expiry.
Native tests do not reproduce
TLS handshakes, Wi-Fi scheduling, or asynchronous SDK allocations.

The radar allocator also counts allocation headers and staging storage and checks
total heap headroom and contiguous allocation fit. The theme allocator added upstream counts
payload bytes and has no reserve callbacks, so substituting it would change these
guarantees.

## Firmware size comparison

Historical measurements below predate the latest deadline/reserve review fixes.
They must not be read as size measurements of the current head.

Matching builds use untouched upstream `a701862` (main following v3.9.0) and
this PR with the same pinned PlatformIO/toolchain packages. They exclude #102,
custom allocation diagnostics and IRAM/CPU experiments. Flash is linked usage
reported by PlatformIO, not `.bin` length. All sizes below are bytes.

| Image | Flash before | Flash after | Change | Static RAM before | Static RAM after | Change |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `smalltv` | 841,841 | 848,241 | +6,400 | 57,152 | 57,236 | +84 |
| `smalltv_lean` | 672,865 | 679,381 | +6,516 | 48,828 | 48,896 | +68 |

A second exported control retains StreamUtils 1.9.2 but bypasses the chunk
decoder and Transfer-Encoding dispatch, passing the timed stream directly to
`parseAdsb`. This isolates the linked cost of the chunk-decoding path. The
control is used only for size measurement, with no source flag added to the PR.

| Image | Chunk-decoding path flash | Static RAM |
| --- | ---: | ---: |
| `smalltv` | +1,428 | +16 |
| `smalltv_lean` | +1,448 | +0 |

## TLS diagnostic measurement

Starting from the 3.6.0-based feed commit (`f29d5ea`), removing only the two
`getLastSSLError(nullptr, 0)` calls reduced flash by 4,728 bytes on both
`smalltv` and `smalltv_lean`, with no static RAM change. The TLS integer and its
API/UI plumbing are therefore omitted; HTTP status and JSON parse errors remain.
These are controlled A/B measurements, not final full-change size figures.

## Reset baseline

The official 3.5.1 lean binary (SHA-256
`516d780aa9ba4a4efa4a69c68875b754ff0e56c4617e1bbed380695668db5ee0`)
reproduced cause `0x1d`, EPC `0x4000df64`, address `0` after eight HTTP 200 /
parse-failed observations with adsb.fi at 50 km. This establishes that the
signature predates the PR; it does not identify its root cause. No real device
captures, coordinates or credentials are committed.
