---
title: Plane radar
description: A radar scope of nearby aircraft, centred on your location, from the free adsb.lol or adsb.fi feeds or a LAN webhook.
---

Switch **Display → Mode** to **Radar** and set it up in the **Radar** tab. The screen becomes a radar scope centred on where you are: range rings, a home marker in the middle, nearby aircraft as red heading triangles with a speed vector and a callsign or altitude label, traffic outside the ring as dots on the rim, and any airports you add as small markers.

## Set your location

By default the radar is centred on the **device location**, the one you set in the Display tab's **Clock & night mode** card (see [Sunrise and sunset brightness](/smalltv-mod/features/sun/)). In the Radar tab this is the **Use the device location** box, and it is on. While it is on, the radar's own Latitude and Longitude fields are greyed out, because they are not used. If the device location is still 0/0 (not set), the screen asks you to set a location.

To centre the radar somewhere else, for example on an airport, untick **Use the device location**. The fields become editable. Enter the latitude and longitude in decimal degrees (four decimals are kept). Tick the box again to go back to the device location. The radar's own coordinates stay saved while the box is on, so unticking it brings them back.

Also choose a range ring (5, 10, 15, 20, 25, or 50), and whether to measure in km or miles. Then pick a data source, below.

A radar set up on an older firmware, with its own coordinates, keeps them: the box starts unticked for it.

## Display options

The Radar tab's **What to show** card tunes what appears. Alongside the two settings below it carries independent toggles for the callsign and altitude labels, the speed vectors, and the rim dots for traffic beyond the outer ring.

- **Marker and label size** (Small, Medium, Large) scales the triangles, markers, and text together. Medium is the default. Large reads from across the room; Small keeps a busy sky uncluttered.
- **Hide aircraft below (ft)** drops ground and low traffic. Set it to `500` and planes taxiing at the airport disappear; `0` shows everything.
- Callsigns are placed nearest first, and any label that would overlap one already on screen is skipped, so a crowded scope stays legible. The triangle still shows even when its label was dropped.

## Data sources

Three choices, set in the Radar tab. Two fetch a free open-data feed straight from the device; the third goes through a proxy on your own network.

### adsb.lol, the default on the ESP8266

The device fetches the free [adsb.lol](https://adsb.lol) API directly over HTTPS, one request per refresh:

```
GET https://api.adsb.lol/v2/lat/<lat>/lon/<lon>/dist/<nm>
```

No API key. The device parses the feed and keeps the closest 24 aircraft. The endpoint is rate-limited to about one request a second, so keep the refresh interval sensible. The default is 10 seconds.

### adsb.fi, the default on the ESP32

Same shape, same fields, a different host:

```
GET https://opendata.adsb.fi/api/v3/lat/<lat>/lon/<lon>/dist/<nm>
```

### Which direct feed to pick

On the ESP8266, HTTPS is tight on RAM, and the deciding factor is the TLS record size the server uses. The device probes the server's Maximum Fragment Length support so its buffer can stay at 512 bytes; without MFLN it has to fall back to 4 KB, and if the server then sends a record larger than that, the read fails part-way through and the scope stays empty.

adsb.fi moved behind Cloudflare in August 2026. Cloudflare does not negotiate MFLN and ramps its record size up on large responses, so **large direct adsb.fi responses can fail on the ESP8266** when a TLS record exceeds that fallback buffer. Small responses may still succeed. adsb.lol is the configured ESP8266 default, but current testing also found no MFLN negotiation there and TLS records larger than the 4 KB fallback buffer. On an ESP8266, use a local receiver or LAN webhook when direct HTTPS fails.

The ESP32 boards use mbedTLS with dynamically sized buffers and are unaffected, so either feed works there.

If your chosen feed ever goes behind a CDN too, switch to the other one or to the webhook below.

### When the scope stays empty on an ESP8266

Start at the **Radar** line in the Status tab. Since 2.12.1 it reports what the last poll actually did: `ok` with an aircraft count, `skipped, low heap`, `connect failed`, `http error` with the code, `parse failed`, `no aircraft in feed`, or `all filtered out` when traffic was there but your minimum-altitude setting dropped it. The same detail is in `/api/status` under `radar`.

These stages have known causes on the ESP8266:

- `skipped, low heap`: the radar refuses to start a TLS handshake unless a 16,000-byte contiguous block is free (shown in the Status tab next to Free heap). Install `smalltv-mod-firmware-lean.bin`, which compiles out Home Assistant screens and the usage meter, or use the webhook below, which is plain HTTP and skips the check. See [Which release file to download](/smalltv-mod/reference/release-assets/).
- `no aircraft in feed` on every poll while flightradar shows a full sky: older firmware could misparse chunked responses as empty. Radar requests now use HTTP/1.1 and decode chunked bodies before parsing JSON.
- `http error, HTTP 426` with adsb.lol: firmware requesting HTTP/1.0 can be rejected by this feed. Rebuild with the HTTP/1.1 radar fix, or use a LAN webhook. Direct HTTPS can still exceed the ESP8266 TLS buffer even after the HTTP/1.1 fix.

### Custom webhook, a LAN proxy

Point a small proxy (n8n, Node-RED, or a short script) at either feed, filter it down to the nearest few aircraft, and return a small JSON over plain HTTP on your LAN. Set the source to **Custom webhook**, give it the URL, and the device calls:

```
GET <webhookUrl>?lat=<lat>&lon=<lon>&dist=<km>
```

It expects the same `{"ac":[ ... ]}` shape both feeds return, with these fields per aircraft: `lat`, `lon`, `track`, `gs`, `flight`, `hex`, `alt_baro`. The proxy does the filtering and the TLS handshake, which is the most reliable setup on the ESP8266.

### Local readsb / dump1090 receiver

The Custom webhook source can also read a receiver's `aircraft.json` directly
on your LAN over plain HTTP, without a proxy or an external API. For example,
use `http://<receiver>:1090/data/aircraft.json` for an adsb.im feeder's tar1090
service. The radar accepts the receiver's `aircraft` array as well as the
public APIs' `ac` array, retaining only the plotted fields. The receiver serves
its local aircraft list; the webhook's appended lat/lon/dist query parameters
do not filter that static JSON file. Reception coverage determines which
flights are available.

The Status page includes the JSON parser error and any recorded BearSSL error when a direct feed fails. These diagnostics do not resolve the ESP8266 TLS record-size limits.

Radar body reads wait up to the configured HTTP timeout for the next data byte, including between HTTP chunks. This prevents brief TLS packet gaps on ESP32 from being mistaken for end-of-input (`IncompleteInput`).

The response is read as a stream, without buffering its whole body. ArduinoJson
allocates document pools and retained strings as needed, within a fixed total
allocation budget (including its filter
and allocator metadata): 20 KB on ESP8266 and 64 KB on ESP32. The ESP8266 also
checks a 4 KB live-heap reserve before each allocation or growth. These are
conservative guards, not measured minimum operating heaps. The allocation
ceiling is not a promise that this much heap is available; the live reserve
can reject a smaller response. The ESP8266 cap
allows a synthetic 60-aircraft receiver list with all seven plotted fields
and unique callsigns and hex IDs in a 32-bit host test. The allocation guard
also applies to existing API feeds because both array formats use the same
parser; adding receiver input must not allow input-sized heap growth. A receiver list
that exceeds the budget reports `parse failed` with `NoMemory`, preserving
the last successful aircraft list. This bounds parser memory; it does not
remove TLS buffer limits or establish the cause of a device reset.
