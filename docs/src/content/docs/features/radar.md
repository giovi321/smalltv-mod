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

Three choices, set in the Radar tab. Two fetch a free open-data feed directly; Custom webhook accepts a LAN proxy or a local receiver URL.

### adsb.lol, the default on the ESP8266

The device fetches the free [adsb.lol](https://adsb.lol) API directly over HTTPS, one request per refresh:

```
GET https://api.adsb.lol/v2/lat/<lat>/lon/<lon>/dist/<nm>
```

No API key. The device parses the feed and retains up to 24 nearest aircraft with numeric coordinates that pass the minimum-altitude filter. The default refresh interval is 10 seconds.

### adsb.fi, the default on the ESP32

Same shape, same fields, a different host:

```
GET https://opendata.adsb.fi/api/v3/lat/<lat>/lon/<lon>/dist/<nm>
```

### Which direct feed to pick

On ESP8266, either direct feed can fail when TLS records exceed the 4096-byte fallback buffer or too little heap remains for packets and parsing; a small JSON response does not guarantee either will fit. adsb.lol remains the default, while a local receiver or LAN proxy over plain HTTP avoids that TLS cost. ESP32 uses a different TLS stack and has more RAM, but network, timeout, memory and input errors can still prevent an update.

### When the scope stays empty on an ESP8266

Start at the **Radar** line in the Status tab. Since 2.12.1 it reports what the last poll actually did: `ok` with an aircraft count, `skipped, low heap`, `connect failed`, `http error` with the code, `parse failed`, `no aircraft in feed`, or `all filtered out` when entries were present but none had numeric coordinates and passed the minimum-altitude filter. The same detail is in `/api/status` under `radar`.

Use these outcomes to narrow down a failure:

- `skipped, low heap`: the radar refuses to start a TLS handshake unless a 16,000-byte contiguous block is free (shown in the Status tab next to Free heap). The lean image omits Home Assistant screens, the usage meter and theme clocks to leave more heap. A LAN URL using plain HTTP skips this TLS check. Neither choice guarantees every request will succeed. See [Which release file to download](/smalltv-mod/reference/release-assets/).
- `no aircraft in feed` on every poll while flightradar shows a full sky: older firmware could misparse chunked responses as empty. Radar requests now use HTTP/1.1 and decode chunked bodies before parsing JSON.
- `parse failed, JSON NoMemory`: the bounded parser could not obtain the memory it needed, or an individual retained value was too large.
- `connect failed`: a negative HTTPClient result can include connection refusal, connection loss or a response timeout, not just failure to open a socket.
- `http error, HTTP 426` with adsb.lol: firmware requesting HTTP/1.0 can be rejected by this feed. Update to 3.5.1 or later, or use a LAN webhook. Direct HTTPS can still exceed the ESP8266 TLS buffer even after the HTTP/1.1 fix.

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

## Parsing and failure handling

Radar parses one aircraft at a time and keeps the nearest 24, so a busy receiver does not need to filter its list to fit a whole-response JSON document. Failed polls preserve cached aircraft, and Status reports HTTP/client results and JSON errors, including `NoMemory`. Reads wait through brief network gaps using the configured timeout, but a synchronous fetch can still delay the web interface.
