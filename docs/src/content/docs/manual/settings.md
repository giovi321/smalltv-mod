---
title: Settings explained
description: Every tab in the settings page, and every option in it, in plain language.
---

The settings page is a single page with tabs across the top: Status, WiFi, Display, Ticker, Clawdmeter, Radar, System. Not every device has all of them: a unit built without the radar feature, for example, has no Radar tab. This page goes through each tab. For the deeper technical reference (data source details, exact request formats) see [Data sources](/smalltv-mod/reference/data-sources/) and each mode's own page.

One button applies everything: **Save settings**, at the very bottom of the page, under every tab. Nothing you type takes effect until you press it. If a save fails, a message on the page says why, and the device keeps the settings it had before.

## Status

Read-only. Shows the firmware version, whether the device is online, its network name, IP address, signal strength, free memory, how long it has been running since the last restart, and why it last restarted. On a device with the WireGuard client, a VPN line says whether the tunnel is up.

The live ticker values used to be here; they now sit at the top of the Ticker tab, next to the settings that produce them.

Free memory and "last reset" are mostly useful when something is wrong; see [Troubleshooting](/smalltv-mod/manual/troubleshooting/).

## WiFi

**Saved networks**: up to four. Press **Scan networks** to see what is nearby, tap one to fill a row, then type its password. At every start the device joins whichever saved network it can see with the strongest signal, and switches to another saved one if the connection drops. Leaving a password field blank when editing an already-saved network keeps the password it already has, it does not clear it.

Networks must be 2.4 GHz. If you cannot see your network in the scan, see [Troubleshooting](/smalltv-mod/manual/troubleshooting/#my-wifi-network-is-not-in-the-scan-list).

**Device name (hostname)**: the name used in `http://<name>.local`. Every device ships with a unique default like `smalltv-3fa2` so several units can share a network without clashing; rename it to something memorable (`smalltv-kitchen`) if you like. Saving a new name restarts the device.

**WireGuard VPN**: only on devices whose firmware includes it (the ESP32-C2, the SmallTV Pro, and an NM-TV-154 running the `-esp32-wg` image). A tunnel that lets you reach the settings page from outside your home network without opening a port to the internet. Full walkthrough in [WireGuard VPN](/smalltv-mod/features/wireguard/); the short version is that you generate a key pair, paste the private half here and the public half into your VPN server, and fill in the server's public key, its address and port, the address this device should have inside the tunnel, and which addresses it should route through it. The line above the fields says what the tunnel is doing, and names the likely cause when it is not up.

**Setup hotspot (AP)**: the name and password of the temporary network the device creates when it has no WiFi to join, or cannot reach any of its saved ones. Change these if you want a different setup network name, or leave them as they are; most people never touch this section again after the first setup.

## Display

**Mode**: which of the three features is on screen, named after the tab that configures it, or **Carousel** to rotate between the ones you tick below it, staying on each for the number of seconds you set. Each mode is configured in its own tab regardless of whether it is the active one, so you can set up the ticker and the radar ahead of time and only switch to Carousel once both are ready.

**Screen**: brightness as a percentage; "Auto-brightness" if your unit has a light sensor and you want the screen to follow the room instead of a fixed level; orientation, if the device is mounted sideways or upside down; and a fix for a dark screen with the backlight visibly on, "Backlight is active-low", which is on by default and should stay that way unless the screen looks wrong.

**Colour**: the panels fitted to these devices are not all the same part, so the same firmware can look warm on one unit and green on another. Turn a channel down with the Red, Green, and Blue sliders until the screen matches what you expect; 100 percent on all three is the untouched image, and "Reset to 100%" puts it back. Two more fixes for panels that are wired differently: "Colour order" swaps red and blue for a unit that shows them the wrong way round, and "Invert the panel" is for a screen that looks like a photographic negative. Saving applies all of this immediately, so you can watch the screen while you adjust.

**Clock & night mode**: pick your timezone by name (for example `Europe/Zurich`) from the dropdown; daylight saving is applied automatically, you never touch it again. Turn on "Dim or blank the screen on a nightly schedule" to set a from-time, a to-time, and a night brightness, where 0 turns the backlight fully off and anything else just dims it. This needs the internet once, at the times it checks, to know what time it is; it does not need it constantly, and the device stays reachable through the night either way.

## Ticker

**Live data**: the current value the device holds for every ticker, with the ones whose last fetch failed in red and the wait before the next attempt next to them. "Refresh data now" asks the device to fetch everything immediately instead of waiting for the next scheduled poll.

**Rotation & data**: how long each symbol stays on screen, how often the device re-checks prices, the chart's timeframe (a day, a month, a year, and so on), how many points the chart draws, and what the "change" and "% change" numbers measure: the whole chart's span (default) or the classic since-yesterday figure. The full explanation of that last one, with an example of when they disagree, is in [Stock and crypto ticker](/smalltv-mod/features/ticker/).

**Color scheme**: swap which colour means up and which means down, for people used to the reverse convention.

**What to show**: tick or untick the name, price, change line, chart, timeframe label, "updated N seconds ago" line, rotation dots, and the portfolio page, independently.

**Tickers**: up to eight rows, each with a symbol, an optional display name, a data source, and an optional quantity and cost that turn it into a position (see [Everyday use](/smalltv-mod/manual/everyday/#the-portfolio-page)). Yahoo Finance is the default source and needs nothing else set up; the other sources and what their symbol field expects are covered in [Data sources](/smalltv-mod/reference/data-sources/).

**cash.ch symbol finder**: paste a link from cash.ch, an ISIN, or an instrument name, press Find, and click a result to add it as a ticker automatically. This runs the search from your own browser, not from the device.

## Clawdmeter

Two fields: the address of the daemon running on your PC, the small program that reads your Claude usage and sends it to the device, and how often the device re-checks it. Leave it blank if that program is pushing to the device; fill it in with the PC's address if the device should instead pull from it.

**Hide the 5-hour usage card**: for plans without a 5-hour limit. The screen then shows only the 7-day usage, in a larger single-card layout with an even-pace marker; see [Hiding the 5-hour card](/smalltv-mod/features/usage/#hiding-the-5-hour-card). Full setup for that PC-side program is in [Claude usage meter](/smalltv-mod/features/usage/), a separate download from a separate project.

## Radar

**Home location**: your latitude and longitude, in decimal degrees. This is the point the radar is centred on. Until these are set, the radar screen shows "Set home location" instead of a scope.

**Range & data**: how far out the scope reaches, in kilometres or miles, how often it refreshes, and where it gets aircraft data from: directly from the free adsb.lol or adsb.fi services (no setup needed) or from your own webhook. All three are explained in [Plane radar](/smalltv-mod/features/radar/).

**What to show**: the size of the markers and labels, a minimum altitude below which aircraft are hidden (useful for filtering out ground traffic at a nearby airport), and independent toggles for callsign/altitude labels, speed vectors, and the rim dots for traffic beyond the outer ring.

**Airports**: up to six, each an ICAO code (for example `LSZH` for Zurich) with its own latitude and longitude, drawn as small markers on the scope.

## System

**Update from GitHub**: shows the installed version, and "Check for latest" compares it against the newest release. If a newer one exists, "Update now" downloads and installs it, restarting the device once or twice depending on the model.

**Manual update (OTA)**: upload a firmware file by hand instead, useful if the device cannot reach GitHub or you built the firmware yourself.

**Settings backup**: "Export settings" downloads the device's whole configuration as a file, including saved WiFi passwords and the WireGuard private key in plain text, so store that file the way you would store a password. "Import" applies a previously exported file and restarts the device; useful when replacing a unit or copying one device's setup to another.

**Password**: off by default, which is why the settings page opens for anyone on your network. Tick "Ask for a password to open this page", set a username and password, and save; from then on the browser asks for them before showing the page, and the same applies to the firmware upload and everything the page talks to. The one exception is the address the Clawdmeter program on your PC pushes to, which has no way to send a password and can only change the numbers on the screen. There is no way to recover a forgotten password: the only way back into a locked device is to reinstall the firmware over a cable. Write it down somewhere before you save.

**Maintenance**: "Reboot" restarts the device without changing anything. "Factory reset" erases every saved setting, including WiFi networks, and puts the device back into first-time SETUP MODE; it does not remove or downgrade the firmware itself.
