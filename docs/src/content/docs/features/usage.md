---
title: Claude usage meter
description: Show your Claude 5-hour and 7-day usage with an animated mascot, fed over WiFi from your PC.
---

Switch **Display → Mode** to **Claude usage** and the device stops showing tickers and shows your Claude consumption instead. It is the idea of a desk usage meter, on the SmallTV, over WiFi. The device's USB is power only, so nothing is wired between it and your PC.

## What it shows

The screen has two states.

- **Stats**, when data is flowing: a small mascot, your 5-hour and 7-day utilization as big percentages with fill bars that shade green to amber to red as you approach the cap, and reset countdowns.
- **Idle animation**, when data stops: if the PC sleeps, the daemon stops, or the network drops, the screen plays an animated pixel mascot. Its mood, from calm to working to dancing, reflects how fast your session is burning. It snaps back to the stats as soon as data returns.

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

   On Windows it runs with a system-tray icon and can auto-start at login. See the [clawdmeter-daemon README](https://github.com/giovi321/clawdmeter-daemon) for tray setup and a durable login token.

2. In the web UI open **Display → Mode → Claude usage**. For push, leave the **Usage URL** blank. For serve and pull, set it to `http://<that-pc-ip>:8787/`. Save.

The mascot animations are a curated subset of the [claudepix](https://claudepix.vercel.app) pixel-art set, re-rendered on the ST7789.
