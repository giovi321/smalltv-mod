---
title: Claude usage meter
description: Show your Claude 5-hour and 7-day usage with an animated mascot, fed over WiFi from your PC.
---

Switch **Display → Mode** to **Clawdmeter** and the device stops showing tickers and shows your Claude consumption instead. It is the idea of a desk usage meter, on the SmallTV, over WiFi. The device's USB is power only, so nothing is wired between it and your PC.

This feature is not in `smalltv-mod-firmware-lean.bin`. That ESP8266 image compiles it out to free 1,080 bytes of heap, and the web UI hides the tab when it is missing. Use `smalltv-mod-firmware.bin` on an ESP8266 that needs the usage meter, or any of the ESP32 images.

## What it shows

The screen has two states.

- **Stats**, when data is flowing: a small mascot, your 5-hour and 7-day utilization as big percentages with fill bars that shade green to amber to red as you approach the cap, and reset countdowns.
- **Idle animation**, when data stops: if the PC sleeps, the daemon stops, or the network drops, the screen plays an animated pixel mascot. Its mood, from calm to working to dancing, reflects how fast your session is burning. It snaps back to the stats as soon as data returns.

### Hiding the 5-hour card

Plans without a 5-hour limit can drop that card: tick **Hide the 5-hour usage card** in the **Clawdmeter** tab and save. The stats screen then shows one tall 7-day card instead of two:

- the 7-day percentage, larger than in the two-card layout
- a thicker fill bar with a white marker at the even pace, the share of the week already elapsed
- the reset countdown
- an "Even pace" line with that share, in amber once your usage runs 5 points or more ahead of it

Even pace is how much of the weekly limit you would have used by now if you spread it evenly over the 7 days. The device works it out from the reset countdown: (7 days minus the time until reset) divided by 7 days. Read the bar against the marker:

- fill ends before the marker: you are below an even spread and have room left
- fill goes past the marker: you are using faster than an even spread, and at this rate you will hit the limit before the reset

For example, 2 days into the week the marker sits at 29%. With usage at 45% the fill runs past it and the "Even pace 29%" line turns amber. The marker and the line appear only in this single-card layout, not on the two-card screen.

With the card hidden, the idle animation's mood follows the 7-day figure instead of the 5-hour one. The setting is stored as `usage.hide5h` in the device config and is off by default.

## Setup

The PC-side daemon is a separate project, [clawdmeter-daemon](https://github.com/giovi321/clawdmeter-daemon). It reads the OAuth token Claude Code already stored and sends your usage to the device. The token never leaves your machine; the device only ever receives a few percentages over your LAN.

1. On the PC that runs Claude Code:

   ```sh
   git clone https://github.com/giovi321/clawdmeter-daemon
   cd clawdmeter-daemon
   pip install -r requirements.txt

   # push to the SmallTV (works even behind WiFi client isolation):
   python clawdmeter_daemon.py --push-to <smalltv-ip-or-hostname>

   # or serve and let the device pull:
   python clawdmeter_daemon.py --serve          # http://0.0.0.0:8787/
   ```

   On Windows, macOS and Linux it runs with a tray / menu-bar icon and can auto-start at login (`--install`). See the [clawdmeter-daemon README](https://github.com/giovi321/clawdmeter-daemon) for tray setup and a durable login token.

   Use **clawdmeter-daemon v1.1.0 or newer**: from that release the daemon reads Claude's usage endpoint rather than spending an inference call per poll, and it reports the session limit as `normal` / `warning` / `rejected`. This firmware accepts that vocabulary and the older `allowed` / `allowed_warning` / `rejected` one.

2. In the web UI open **Display → Mode → Clawdmeter**. Then open the **Clawdmeter** tab: for push, leave the **Usage daemon URL** blank; for serve and pull, set it to `http://<that-pc-ip>:8787/`. Save.

The mascot animations are a curated subset of the [claudepix](https://claudepix.vercel.app) pixel-art set, re-rendered on the ST7789.

### Multiple devices (auto-discovery)

Each device advertises a `_clawdmeter._tcp` mDNS service. The daemon can use this to discover every SmallTV on the LAN and push to all of them at once, no per-device address needed:

```sh
python clawdmeter_daemon.py --push
```

IMPORTANT: mDNS is link-local and does not cross routers, subnets, or VLANs. If the daemon PC and the devices sit on different subnets, do one of:

- run the daemon on the devices' subnet
- enable an mDNS reflector between the subnets
- list the device IPs explicitly instead of relying on discovery, with repeatable or comma-separated `--push-to`, or via the tray icon's **Configure push targets** dialog
