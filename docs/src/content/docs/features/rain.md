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
- **Refresh (s)** is how often the whole frame set is fetched. The default is 900 (15 minutes). RainViewer's public limit is generous, but keep it at a few minutes or more.
- **Animation (s/frame)** is how long each frame stays on screen in the loop. The default is 8 seconds.

The RainViewer public API serves **past radar data only**: the timeline runs from two hours ago to the latest frame, labelled `-2h`, `-1h` and `now` (the current frame's local time is shown top-right once the clock is synced). The firmware also understands the *nowcast* frame list, so if the provider ever serves forecast frames again they will appear on the timeline automatically.

## What to show

- **Nearby cities** toggles the reference dots: cities of France and Europe embedded in the firmware (from GeoNames `cities5000`, CC-BY 4.0), filtered by the range, the most populous ones labelled.

## How it works

Data comes from [RainViewer](https://www.rainviewer.com), no API key required, and is used under their [personal-use terms](https://www.rainviewer.com/api/weather-maps-api.html) (attribution required — the screen credits RainViewer in a corner and this page names the source).

Every refresh the device fetches the frame list (`weather-maps.json`) and then one 256 px PNG per frame, centred on your location. RainViewer serves radar imagery at **zoom 7**; higher zooms return map tiles, so the firmware takes the zoom-7 tile and crops the centre to roughly twice the configured range, then upscales it to the full 240×240 screen with bilinear interpolation.

The PNGs decode with a decoder written for this firmware rather than a generic library: the tiles use the PNG "None" filter on every row, so the decoder's sliding window stores one RGB565 value per pixel — 16 KB of heap instead of the 32 KB a full decoder needs — and quantizes the RainViewer colour ramp into a 16-level blue-to-purple palette on the fly. That is what lets the mode run on the ESP8266, whose lean image has about 23 KB of heap to spare.

On any network or provider failure the old frames stay on disk and keep looping, and a small red dot appears in the corner. The next scheduled refresh retries.

## Status

The **Status** tab reports the rain pipeline under `rain`: how many frames are on disk, the outcome of the last poll (`ok`, `http error`, `no frames in feed`, …), the last HTTP code, the crop size used, and the age of the last successful fetch.
