---
title: Troubleshooting
description: Fixes for the problems a non-technical user is most likely to run into.
---

Common problems and what to do about them, in the everyday terms someone using the device day to day would reach for. For flashing and recovery problems (the device stuck after installing the firmware, or reverting to the original software), see [Flashing](/smalltv-mod/getting-started/flashing/) and [Recovery and credits](/smalltv-mod/reference/recovery/) instead.

## I can't find the settings page anymore

Unplug the device and plug it back in. For a few seconds after it reconnects, the screen shows either:

- a green **CONNECTED** screen with its IP address and a `.local` name address, if it already knows a WiFi network, or
- a yellow **SETUP MODE** screen with a hotspot name and `http://192.168.4.1`, if it does not (see [Quick start](/smalltv-mod/manual/quick-start/))

Whichever screen you get tells you exactly where to browse. Your phone or computer needs to be on the same WiFi network as the device; this does not work over mobile data.

If the `.local` address does not open in your browser, try the plain IP address instead. Some networks and some browsers do not support `.local` names well.

## My WiFi network is not in the scan list

- The device only sees **2.4 GHz** networks. If your router shows separate `_2.4G` and `_5G` names, connect it to the 2.4 GHz one. If it shows one combined name for both bands, most routers still let 2.4 GHz devices join through it, but a few do not; check your router's WiFi settings for a way to split the bands temporarily.
- Move the device closer to the router for the scan, then move it back afterwards if needed; a saved network with a weak signal still works, but it needs to be visible to appear in the scan list at all.
- Hidden networks (ones that do not broadcast their name) never appear in a scan. Skip scanning and type the name directly into a WiFi row before entering the password.
- Press **Scan networks** again; a first scan sometimes misses a network that a second one catches.

## A symbol shows "fetch error" or stays on "loading"

- Double check the spelling. Yahoo Finance symbols are case-sensitive-looking but usually written in capitals: `AAPL`, not `apple`. Swiss and European stocks need their exchange suffix, `NESN.SW` not `NESN`.
- A brand-new symbol takes a few seconds to resolve after you save it; give it one full refresh cycle before assuming it is wrong.
- A failed symbol is retried on its own, without you doing anything: after 12 seconds, then 24, 48, 96, and from there at the normal refresh interval for as long as it keeps failing. The other symbols are not dragged into those retries and keep their usual schedule. The Ticker tab's **Live data** card shows which ones are failing and how long until the next attempt.
- Press **Refresh data now** in the Ticker tab to force an immediate retry rather than waiting for that countdown.
- If it is a cash.ch source symbol, it needs the exact listing key format, not a plain ticker; use the **cash.ch symbol finder** in the Ticker tab to generate it rather than typing one by hand.
- If every symbol shows the error at once, the device likely lost its connection; check the Status tab for whether it still shows as connected, and see the WiFi section below.

## The device shows the wrong time, or night mode isn't dimming the screen

The device gets the time from the internet, and it only checks it when something needs it: night mode, or a WireGuard tunnel on the models that have one. The clock line in the Display tab reports on night mode only, so on a device using the clock purely for a tunnel it still reads as though NTP were idle. Either way, none of this affects the ticker or the radar.

- Give it a minute after saving: the first check happens shortly after you enable night mode, not instantly.
- Confirm the timezone in the Clock & night mode card matches where you are; a correct clock in the wrong timezone will dim at the wrong local time.
- If the device cannot reach the internet at all, it leaves the screen on rather than guess, and keeps retrying until it succeeds or the night window ends for the day. This is intentional: it never gets stuck dark.
- Right after a restart, the screen may show full brightness for a few seconds inside the night window, until the next time check lands.

## The radar scope is empty, or a ticker is blank, with no error shown

This is the original ESP8266 model running short of memory. Both features check that enough is free before opening an HTTPS connection, and when there is not they skip the attempt rather than crash, so nothing appears on screen to explain it. Both want a 16,000-byte contiguous block for the TLS handshake.

Since 2.12.1 the Status tab has a **Radar** line naming the last poll's outcome; `skipped, low heap` is this case, spelled out. The largest free block is shown next to Free heap on the same page. When the block sits under 16,000, memory is the cause and no amount of changing the data source will help, because the device never gets as far as sending a request.

Three things bring it back, in the order worth trying:

1. Install `smalltv-mod-firmware-lean.bin` from the [Releases page](https://github.com/giovi321/smalltv-mod/releases). Same code, with Home Assistant screens and the usage meter compiled out, which leaves the heap 8,732 bytes larger. Upload it in the System tab like any other update; your settings carry over. See [Which release file to download](/smalltv-mod/reference/release-assets/)
2. Improve the WiFi signal. A weak link means constant retransmissions, and the queues holding them come out of the same memory. A device sitting at -75 dBm has noticeably less heap free than the same device close to the access point
3. Switch the radar to a **Custom webhook** over plain HTTP. The memory check only guards the HTTPS path, so a LAN proxy sidesteps it entirely. [Plane radar](/smalltv-mod/features/radar/) has the setup

A busy office or lab network makes this worse than a quiet home one. Broadcast and multicast traffic all gets buffered by the device, so the same firmware on the same hardware can work on one network and go quiet on another.

The ESP32 models have several times the RAM and manage their TLS buffers dynamically, so none of this applies to them.

## A ticker stopped working after I turned on night mode

This is specific to the original ESP8266 model with cash.ch tickers. Night mode's clock check and a cash.ch fetch both need a chunk of the device's limited memory at the same time, and on this older chip that can be too much at once, so the cash.ch ticker starts failing. Two fixes, either one works:

- switch that ticker's source from **cash.ch** to **GitHub** in the Ticker tab; it fetches the same instrument through a route that costs less memory (you publish the quote files yourself from a fork, see [Data sources](/smalltv-mod/reference/data-sources/)), or
- turn night mode off on that particular device

The newer ESP32-based models have more memory and are not affected by this.

## It saves my WiFi, restarts, and comes back to SETUP MODE

The device tried the saved network for 30 seconds and gave up. The red line under the SETUP MODE title names the network and the reason, and the System tab shows the same text as "Last join failure":

- "wrong password?" means the router turned the password down. Retype it in the WiFi tab, watching for spaces and capital letters.
- "auth timed out" means the router did not finish checking the password in time. A wrong password can cause it, but so can a weak signal or a busy hotspot: retype the password, then move the device closer.
- "network not found" means the network was not in range on 2.4 GHz. The device has no 5 GHz radio, and a router on channel 12, 13 or 14 can be invisible to it.
- "security mode?" means the router asks for a security mode the device does not offer. Set the 2.4 GHz network to WPA2-PSK (AES), or to WPA2/WPA3 mixed mode without "PMF required".
- "no IP (DHCP)" means the device joined but the router handed out no address. Check the router's DHCP pool and any MAC filter.
- "timed out" means the router never answered within 30 seconds. Move the device closer, and check that the router's 2.4 GHz radio is on a channel from 1 to 11.

The number in brackets is the WiFi driver's reason code, worth quoting in an issue. While the hotspot is up and nobody is connected to it, the device retries the saved networks every 90 seconds and restarts on its own once one works.

## I forgot my WiFi password and need to move the device

You do not need the old password to add a new network. Open the WiFi tab, scan, tap the new network, enter its password, and save; up to four networks can be saved at once, so the old one does not need to be removed first.

If the device cannot reach any saved network at all (say, after a full house move), it falls back to its own SETUP MODE hotspot automatically, and you start over from [Quick start](/smalltv-mod/manual/quick-start/).

## The WireGuard tunnel will not come up

The ESP32-C2 and the SmallTV Pro carry the client, and so does an NM-TV-154 flashed with the `-esp32-wg` image; the ESP8266 boards and a plain-image NM-TV-154 have no WireGuard card in the settings page at all. If the card is missing on an NM-TV-154, check the variant next to the version in the System tab: it reads `esp32` on the image without the tunnel and `esp32-wg` on the one with it. The line at the top of the card names the problem, so read that first.

- "Waiting for the endpoint name to resolve" means DNS has not answered. Check the endpoint hostname, or put the server's IP address there instead.
- "Handshakes sent with no reply" means the device reached the server and got nothing back. The server needs a peer entry carrying this device's public key, and UDP on the endpoint port has to reach it.
- "Tunnel suspended after repeated reboots" means the device crashed three times in a row with the tunnel enabled and stopped starting it. Check the settings and press **Save settings** again; re-saving is what clears the hold.
- The tunnel is up but the device is unreachable at its tunnel address: this is almost always the Allowed IPs field. List the subnet the other end is on (`10.6.0.0/24`), not only the device's own address. [WireGuard VPN](/smalltv-mod/features/wireguard/) explains why.

## Saving settings says "not enough memory"

The device did not have enough free memory to process the save, so it refused it and kept the settings it had. This happens on the original ESP8266 model, most often while a clock-face theme is running. The running theme is already freed before a save when memory is short. If the message persists, switch the display away from the theme, save, then switch back.

Before v3.0.0 the same situation could erase every setting, WiFi included, and restart the device in SETUP MODE. Update to v3.0.0 or later, and restore from a settings export if you have one.

## An update to v2.16.0 or v2.17.0 fails with "Not Enough Space"

On the SmallTV (ESP32-C2) and with the NM-TV-154 WireGuard image (`-esp32-wg`), those two releases were too large for the update slot, so the device refused them and kept running its current version. v3.0.0 fits; update to it from the System tab as usual.

## I pressed Factory reset and lost my settings

Factory reset is meant to erase everything, so this is expected: it wipes saved WiFi networks, tickers, and every other setting, and restarts the device in first-time SETUP MODE. It does not touch the firmware itself, so no reinstall is needed. Set the device up again as in [Quick start](/smalltv-mod/manual/quick-start/).

If you exported a settings backup beforehand (System tab, "Export settings"), importing that file restores everything at once instead of re-entering it by hand.

## The screen stays dark even though the device is powered

- Check brightness in the Display tab is above 0, and that a night-mode window is not currently active with its night brightness set to 0 (which turns the backlight fully off on purpose).
- If the backlight circuit is inverted from what the firmware expects, the screen can appear dark even at full brightness; toggle "Backlight is active-low" in the Display tab.
- If the device does not respond to the settings page at all, it may not be reachable on the network; see "I can't find the settings page anymore" above.

## The screen shows CRASH

Rare, but if it happens: the device restarted after an internal fault, and stays on this screen instead of trying to redraw whatever crashed it. It shows a fault code and its IP address, and the settings page and firmware update are still reachable at that address. Reinstalling the latest firmware version through the System tab normally clears it, and no settings are lost by doing so.

## Updating from GitHub fails

- On the original ESP8266 model, firmware version 2.6.1 and earlier cannot update themselves due to a bug in that version's updater; a broken updater cannot fix itself. Update that device once by hand as described in [Flashing](/smalltv-mod/getting-started/flashing/#after-the-first-flash), and the automatic updater works normally afterwards.
- A failed update leaves the device running its current version; nothing is lost, and the System tab shows the reason for the failure. Usually pressing "Check for latest" and "Update now" again succeeds on a retry.
- The download needs the device's normal internet connection; if WiFi is unstable at the time, wait and try again.

## Something else

Open an issue on the project's [GitHub page](https://github.com/giovi321/smalltv-mod/issues) with your board type (from the chip badge in the settings page header), the firmware version (Status tab), and what you tried.
