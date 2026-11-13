---
title: Rain radar
description: The last 2 hours of rain around your location as a RainToday-style scope, from RainViewer's free API, refreshed every 15 minutes.
---

Switch **Display → Mode** to **Rain** and set it up in the **Rain** tab. The screen becomes a radar scope centred on where you are: range rings and a crosshair, the rain field in a blue-to-purple ramp (drizzle in light blue, heavy rain in violet), nearby cities as reference dots, a green dot for your position, and a timeline at the bottom spanning the last 2 hours. The frames loop as an animation between refreshes, and the whole set refreshes every 15 minutes (10-minute radar steps).

## Set your location

By default the radar is centred on the **device location**, the one you set in the Display tab's **Clock & night mode** card (see [Sunrise and sunset brightness](/smalltv-mod/features/sun/)). In the Rain tab this is the **Use the device location** box, and it is on. While it is on, the radar's own Latitude and Longitude fields are greyed out. If the device location is still 0/0 (not set), the screen asks you to set a location.

To centre the radar somewhere else, untick **Use the device location** and enter the latitude and longitude in decimal degrees (four decimals are kept). The radar's own coordinates stay saved while the box is on.

## Range and cadence

- **Range (km)** is the outer ring radius, 10 to 100 km. The firmware picks the largest usable image crop for the configured range at your latitude; at higher latitudes the same range simply uses a larger source crop, so nothing is lost.
- **Refresh (s)** is how often the whole frame set is fetched. The default is 900 (15 minutes), the minimum 600: a full fetch takes ~5–6 minutes on the device, and the next poll is always scheduled from the end of a fetch — the device never fetches back-to-back.
- **Animation (s/frame)** is how long each frame stays on screen in the loop. The default is 8 seconds.

The RainViewer public API serves **past radar data only**: the timeline runs from two hours ago to the latest frame, labelled `-2h`, `-1h` and `now` (the current frame's local time is shown top-right once the clock is synced). The firmware also understands the *nowcast* frame list, so if the provider ever serves forecast frames again they will appear on the timeline automatically.

## What to show

- **Text & city size** scales the overlay texts and the city squares: **Small** keeps the regular 8 px glyphs and shrinks the markers (3×3 / 2×2), **Medium** (default) doubles the texts (16 px) with 5×5 / 3×3 squares, **Large** jumps the `N` and the timeline labels to 24 px and doubles the squares (10×10 / 6×6). The headers and city names stay at 16 px on Large: the built-in font only scales in whole steps, and a 24 px header would cross the scope ring while the longest names would span nearly the whole panel. The RainViewer credit stays small so it never collides with the timeline.
- **Nearby cities** toggles the reference dots: cities of France and Europe embedded in the firmware (from GeoNames `cities5000`, CC-BY 4.0), filtered by the range, the most populous ones labelled.

## How it works

Data comes from [RainViewer](https://www.rainviewer.com), no API key required, and is used under their [personal-use terms](https://www.rainviewer.com/api/weather-maps-api.html) (attribution required — the screen credits RainViewer in a corner and this page names the source).

Every refresh the device fetches the frame list (`weather-maps.json`) and then one 256 px PNG per frame, centred on your location. RainViewer serves radar imagery at **zoom 7**; higher zooms return map tiles, so the firmware takes the zoom-7 tile and crops the centre to roughly twice the configured range, then upscales it to the full 240×240 screen with bilinear interpolation.

The PNGs decode with a decoder written for this firmware rather than a generic library: the tiles use the PNG "None" filter on every row, so the decoder's sliding window stores one byte per pixel — the 6-bit index of the pixel's displayed colour plus a 2-bit alpha class — 8 KB of heap instead of the 32 KB a full decoder needs. Every pixel is premultiplied on black (the screen's background) and mapped to the nearest colour of a 58-entry table measured from live tiles (a 4 KB lookup table baked into flash at build time), so the screen renders exactly what RainViewer renders — the cloud layer and the Universal Blue rain ramp in their own colours. That is what lets the mode run on the ESP8266, whose lean image has about 14 KB of heap to spare at boot.

On any network or provider failure the old frames stay on disk and keep looping, and a small red dot appears in the corner. The next scheduled refresh retries.

## Status

The **Status** tab reports the rain pipeline under `rain`: how many frames are on disk, the outcome of the last poll (`ok`, `http error`, `no frames in feed`, …), the last HTTP code, the crop size used, and the age of the last successful fetch.
