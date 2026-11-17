# rain_sim — the rain radar screen, simulated on a PC

Replays what the SmallTV shows once plugged in, with the rain mode on:
boot screens, the CONNECTED screen, "Waiting for radar data", the first
fetch (frames arriving one by one, about 20 s each as on the device), then
the animation loop; optionally RainViewer going away ("Radar data
unavailable").

It runs the firmware's own code — `RainMode.cpp`, `RainClient.cpp`,
`RainPng.cpp`, `Settings.cpp` — and the real GFX Library for Arduino (its
drawing core and in-memory canvas), so the pixels are the device's. Only
the hardware is stood in for: the panel is a 240x240 framebuffer
(`sim_gfx.cpp`, the same API as `src/Gfx.cpp`), LittleFS lives in memory and
the network serves real RainViewer tiles downloaded beforehand. Time is
virtual.

```
pio run -e smalltv_lean        # once: fetches the libraries the simulator compiles
python3 tools/rain_sim/simulate.py --lat 48.39 --lon -4.49 --out /tmp/rain-sim
python3 tools/rain_sim/simulate.py --lat 48.39 --lon -4.49 --out /tmp/rain-sim --outage
```

Options: `--range` (km, default 50), `--anim` (s per frame, default 3), `--ui`
(text & city size 0/1/2, default 1), `--tiles DIR` (reuse downloaded tiles),
`--speed` (GIF speed-up of device time, default 8), `--preview FILE` (save the
most colourful radar screen, 2x — how `docs/public/assets/rain-preview.png`
is made).

Output: `screens/NNNN.png` (every distinct screen, 2x), `screens/timeline.csv`
(device time of each), `screens.gif` (the timeline, sped up) and
`contact.png` (each distinct screen once, with its time).

Not simulated: the colour gains of the Display tab (left at 100 %), the
blit's top-to-bottom sweep (a frame appears at once here), the web UI, the
heap. Needs g++, Python 3 and Pillow.
