---
title: Plane radar
description: A radar scope of nearby aircraft, centred on your location, from the free adsb.fi feed or a LAN webhook.
---

Switch **Display → Mode** to **Plane radar** and set it up in the **Radar** tab. The screen becomes a radar scope centred on where you are: range rings, a home marker in the middle, nearby aircraft as red heading triangles with a speed vector and a callsign or altitude label, traffic outside the ring as dots on the rim, and any airports you add as small markers.

## Set your location

Enter your home latitude and longitude in decimal degrees, a range ring (5, 10, 15, 25, or 50), and whether to measure in km or miles. Then pick a data source, below.

## Display options

Three controls in the Radar tab tune what appears.

- **Marker and label size** (Small, Medium, Large) scales the triangles, markers, and text together. Medium is the default. Large reads from across the room; Small keeps a busy sky uncluttered.
- **Hide aircraft below (ft)** drops ground and low traffic. Set it to `500` and planes taxiing at the airport disappear; `0` shows everything.
- Callsigns are placed nearest first, and any label that would overlap one already on screen is skipped, so a crowded scope stays legible. The triangle still shows even when its label was dropped.

## Data sources

### adsb.fi, the default

The device fetches the free [adsb.fi](https://adsb.fi) API directly over HTTPS, one request per refresh:

```
GET https://opendata.adsb.fi/api/v3/lat/<lat>/lon/<lon>/dist/<nm>
```

No API key. The device parses the feed and keeps the closest 24 aircraft. The endpoint is rate-limited to about one request a second, so keep the refresh interval sensible. The default is 10 seconds.

On the ESP8266, HTTPS is tight on RAM. The device probes the server's Maximum Fragment Length support so its TLS buffer can stay small. If adsb.fi ever sends large TLS records without that support, the direct fetch can fail in busy airspace; use the webhook source below if that happens. The ESP32-C2 has more RAM and does not need the probe.

### Custom webhook, a LAN proxy

Point a small proxy (n8n, Node-RED, or a short script) at adsb.fi, filter it down to the nearest few aircraft, and return a small JSON over plain HTTP on your LAN. Set the source to **Custom webhook**, give it the URL, and the device calls:

```
GET <webhookUrl>?lat=<lat>&lon=<lon>&dist=<km>
```

It expects the same `{"ac":[ ... ]}` shape adsb.fi returns, with these fields per aircraft: `lat`, `lon`, `track`, `gs`, `flight`, `hex`, `alt_baro`. The proxy does the filtering and the TLS handshake, which is the most reliable setup on the ESP8266.
