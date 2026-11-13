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
- **Aircraft markers**, **Radar lines**, **Aircraft info**, **Airport labels**, and **General text** independently set the airplane symbols and off-screen dots, grid, callsign/altitude text, airport names, and compass/distance/count text. **Reset colors to defaults** fills the controls with the original palette: red (`#ff0000`) for aircraft markers, dark gray (`#424142`) for lines, blue (`#0082ff`) for airport names, and medium gray (`#848284`) for other text. Save settings to apply. Airport markers, heading vectors, and the radar error indicator keep their original colors. These radar settings are included in backups.
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

adsb.fi moved behind Cloudflare in August 2026. Cloudflare does not negotiate MFLN and ramps its record size up on large responses, so **direct adsb.fi no longer works on the ESP8266**. A 50 nm response is around 50 KB and the connection breaks mid-stream. adsb.lol still honours MFLN, which is why it is the ESP8266 default.

The ESP32 boards use mbedTLS with dynamically sized buffers and are unaffected, so either feed works there.

If your chosen feed ever goes behind a CDN too, switch to the other one or to the webhook below.

### When the scope stays empty on an ESP8266

Start at the **Radar** line in the Status tab. Since 2.12.1 it reports what the last poll actually did: `ok` with an aircraft count, `skipped, low heap`, `connect failed`, `http error` with the code, `parse failed`, `no aircraft in feed`, or `all filtered out` when traffic was there but your minimum-altitude setting dropped it. The same detail is in `/api/status` under `radar`.

Two of those stages have known causes on the ESP8266:

- `skipped, low heap`: the radar refuses to start a TLS handshake unless a 16,000-byte contiguous block is free (shown in the Status tab next to Free heap). Install `smalltv-mod-firmware-lean.bin`, which compiles out Home Assistant screens and the usage meter, or use the webhook below, which is plain HTTP and skips the check. See [Which release file to download](/smalltv-mod/reference/release-assets/).
- `no aircraft in feed` on every poll while flightradar shows a full sky: before 2.12.1 this is what a device pointed at adsb.fi showed after Cloudflare fronted that feed, because Cloudflare's chunked responses misparsed as empty. 2.12.1 requests HTTP/1.0, which cannot be chunked; on older firmware, switch the source to adsb.lol.

### Custom webhook, a LAN proxy

Point a small proxy (n8n, Node-RED, or a short script) at either feed, filter it down to the nearest few aircraft, and return a small JSON over plain HTTP on your LAN. Set the source to **Custom webhook**, give it the URL, and the device calls:

```
GET <webhookUrl>?lat=<lat>&lon=<lon>&dist=<km>
```

It expects the same `{"ac":[ ... ]}` shape both feeds return, with these fields per aircraft: `lat`, `lon`, `track`, `gs`, `flight`, `hex`, `alt_baro`. The proxy does the filtering and the TLS handshake, which is the most reliable setup on the ESP8266.
