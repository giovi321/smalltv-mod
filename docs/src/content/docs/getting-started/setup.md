---
title: First-time setup
description: Join WiFi, reach the web UI, and configure what the SmallTV shows.
---

On first boot the device has no WiFi saved, so it starts its own hotspot and waits for you to point it at your network. Everything after that happens in the web UI.

## Join WiFi

1. On first boot the screen shows **SETUP MODE** and creates an open hotspot named `SmallTV-Setup`.
2. Join it from your phone or PC. A captive portal should open on its own. If it does not, browse to `http://192.168.4.1`.
3. Open the **WiFi** tab, press **Scan**, pick your 2.4 GHz network, enter the password, and press **Save and connect**. The device reboots and joins your network. You can save up to 4 networks; at boot the device joins the strongest one it can see.
4. It shows the joined network, its IP, and its `http://<hostname>.local` address on screen at boot. Browse to either one. New devices get a unique default hostname like `smalltv-3fa2` so several SmallTVs can share a network; rename it in the WiFi tab.

The ESP8266 is 2.4 GHz only. For an AP password use at least 8 characters, or leave it blank for an open hotspot.

## Add something to show

Open the **Ticker** tab and add a few tickers, for example `AAPL`, `NESN.SW`, or `BTC-USD`. Each ticker picks its own data source; the default is Yahoo Finance, so prices appear within a few seconds with no server to set up. Swiss instruments Yahoo lacks can use cash.ch, and a custom webhook lets you own the source. See [Data sources](/smalltv-mod/reference/data-sources/) for the full list of what works.

## Web UI reference

The UI is a single page served from the device. Saving applies most changes live; changing the WiFi network reboots. The header shows a chip naming the board the firmware targets (ESP8266, ESP32-C2, or ESP32).

### Status

Live device info: mode, IP, signal, free heap and the largest free block, uptime, the last reset reason, the radar's last poll and, where the firmware has the WireGuard client, the tunnel state. After a crash it also shows a **Last crash** line (v3.5.0 or later), and a Home Assistant line when that feature is not running as set. The same values are in `GET /api/status`; see [HTTP API](/smalltv-mod/reference/http-api/).

### WiFi

Scan and save up to 4 networks; the device joins the strongest visible one at boot and falls over to the others if the connection drops. Also sets the device hostname (its `.local` mDNS name), the setup hotspot name and password, and on the builds that carry it the [WireGuard tunnel](/smalltv-mod/features/wireguard/).

### Display

The mode selector, which lists each feature under the same name as its tab (Ticker, Clawdmeter, Radar, Home Assistant, Theme clock, or Carousel, which rotates through the ticked features on a timer), plus brightness with optional auto-brightness, orientation, and backlight polarity. A build without a feature leaves it out of the list. The **Theme clocks** card installs, selects and removes `.stheme` packages (see [Theme clocks](/smalltv-mod/features/themes/)), and on the SmallTV Pro and the NM-TV-154 a **Button** card sets what a short tap and a long press of the touch button do, and its sensitivity.

#### Colour

Per-channel Red, Green, and Blue gain (50 to 150 percent, 100 being the untouched image), a colour-order switch for panels that show red and blue swapped, and an invert toggle for panels that come up looking like a negative. These exist because the units do not all carry the same panel: two devices running this firmware side by side can render the same colour visibly differently. Changes apply as soon as you save.

#### Clock and night mode

The "Clock & night mode" card sets a timezone by IANA name (`Europe/Rome`, `Europe/Zurich`, `America/New_York`, and so on) from a dropdown; DST is handled automatically. Enabling the nightly window adds a From and To time (HH:MM) and a night brightness, where 0 turns the backlight fully off and any other value just dims it. Night mode is off by default. The device keeps rendering behind the dimmed or off backlight rather than sleeping, so WiFi, the web UI, and usage push stay up throughout.

Night mode only switches on once NTP has confirmed the clock within the last few minutes. If NTP can't be reached, the screen stays on and the device keeps retrying until it syncs or the window ends in the morning, so a device that never reaches NTP is never left stuck dark. NTP itself only runs when something needs it: night mode, the sunrise and sunset schedule, a theme clock on screen or in the carousel, or a WireGuard tunnel, which cannot handshake without a correct clock. On an ESP8266 this matters if you also use cash.ch tickers: their TLS handshake needs a large contiguous block of heap, and adding night mode's NTP client on top can leave too little. If a ticker stops fetching after enabling night mode, switch it to the GitHub source (self-hosted from a fork, see [Data sources](/smalltv-mod/reference/data-sources/)) or leave night mode off on that device; ESP32 boards aren't affected.

### Ticker

The live value of every ticker sits at the top, with a "Refresh data now" button that forces an immediate poll. Below it, up to 8 tickers, each with its own data source (Yahoo Finance, cash.ch, GitHub, or your webhook). `symbol` follows the source: a Yahoo ticker (`AAPL`, `NESN.SW`, `BTC-USD`, `EURUSD=X`), a cash.ch listing key for cash.ch or GitHub (the built-in finder turns a cash.ch link or ISIN into one), or whatever your webhook expects. `name` is an optional label that overrides the source's own name. Optional `qty` and per-unit `cost` turn a ticker into a position: its page gains a P/L line and a portfolio summary page joins the rotation. The tab also sets the chart timeframe and point count, rotation and refresh intervals, the colour scheme, and which fields to draw.

### Home Assistant

The MQTT card: broker host, port, an optional username and password, and how long each screen stays up. A blank host switches the feature off, and switching it on or off takes effect after a restart. **Clear screens** wipes every screen from the device and the broker's retained copies. See [Home Assistant screens](/smalltv-mod/features/ha/). The lean ESP8266 image has no such tab.

### System

Check for and install the newest GitHub release (every board fetches its own image), upload a firmware file manually, export or import the full device configuration as a JSON file, put the page behind a password, reboot, or factory reset. The exported file contains the WiFi passwords and the WireGuard private key in clear text, so treat it like a password.

## Modes

Each mode has its own page:

- [Stock and crypto ticker](/smalltv-mod/features/ticker/)
- [Claude usage meter](/smalltv-mod/features/usage/)
- [Plane radar](/smalltv-mod/features/radar/)
- [Home Assistant screens](/smalltv-mod/features/ha/)
- [Theme clocks](/smalltv-mod/features/themes/)

Remote access over a tunnel is not a mode, but it has its own page too: [WireGuard VPN](/smalltv-mod/features/wireguard/).
