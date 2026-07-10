---
title: First-time setup
description: Join WiFi, reach the web UI, and configure what the SmallTV shows.
---

On first boot the device has no WiFi saved, so it starts its own hotspot and waits for you to point it at your network. Everything after that happens in the web UI.

## Join WiFi

1. On first boot the screen shows **SETUP MODE** and creates an open hotspot named `SmallTV-Setup`.
2. Join it from your phone or PC. A captive portal should open on its own. If it does not, browse to `http://192.168.4.1`.
3. Open the **WiFi** tab, press **Scan**, pick your 2.4 GHz network, enter the password, and press **Save and connect**. The device reboots and joins your network.
4. It shows the joined network and its IP on screen at boot, along with `http://smalltv.local`. Browse to either one.

The ESP8266 is 2.4 GHz only. For an AP password use at least 8 characters, or leave it blank for an open hotspot.

## Add something to show

Open **Symbols** and add a few tickers, for example `AAPL`, `NESN.SW`, or `BTC-USD`. The default data source is Yahoo Finance, so prices appear within a few seconds with no server to set up. To use your own backend instead, switch **Display → Data source** to *Custom webhook* and paste its URL. See [Data sources](/smalltv-mod/reference/data-sources/) for the full list of what works.

## Web UI reference

The UI is a single page served from the device. Saving applies most changes live; changing the WiFi network reboots.

### Status

Live device info: mode, IP, signal, free heap, uptime, and the current ticker values. "Refresh data now" forces an immediate poll.

### WiFi

Scan and join a network. Configure the hotspot name and password and the mDNS hostname.

### Display

The mode selector (Stock ticker, Claude usage, or Plane radar) and its data URL, plus brightness with optional auto-brightness, orientation, backlight polarity, colour scheme, and the rotation and refresh intervals. This tab also sets the data source (Yahoo Finance or custom webhook), the webhook URL, the chart timeframe and point count, and which fields to draw (name, price, change, chart, timeframe label, "updated N s ago", rotation dots).

### Symbols

Up to 8 tickers. `symbol` is the Yahoo ticker (for example `AAPL`, `NESN.SW`, `BTC-USD`, `EURUSD=X`). `name` is an optional label that overrides the source's own name.

### Update

Upload a firmware image, reboot, or factory reset. On the ESP8266 this tab also checks for and installs the newest GitHub release.

## Modes

Each mode has its own page:

- [Stock and crypto ticker](/smalltv-mod/features/ticker/)
- [Claude usage meter](/smalltv-mod/features/usage/)
- [Plane radar](/smalltv-mod/features/radar/)
