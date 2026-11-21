---
title: Quick start
description: Plug the device in, put it on your WiFi, and get the first price on screen. No app, no account, no cloud service.
---

Start here if someone handed you the device already set up with this firmware, or if you just flashed it yourself. The whole setup happens in a web page the device itself serves, so there is no app to install and no account to create. Ten minutes, and most of that is typing your WiFi password.

If the device still runs the manufacturer's own software (a weather clock, a photo frame, a stock app you cannot configure this way), it needs this firmware installed first. See [Flashing](/smalltv-mod/getting-started/flashing/), or ask whoever gave it to you.

## What you need

- A USB-C power supply. Any phone charger works. The device draws very little.
- Your WiFi network name and password. The network must be **2.4 GHz**; the device cannot see 5 GHz networks. Most home routers broadcast both under the same name, which is fine.
- A phone, tablet, or computer with a browser.

You do not need a computer running anything, a server, an API key, or an internet subscription beyond your normal connection. The one exception is the Claude usage mode, which needs a program on your PC; the ticker and the radar work on their own.

## 1. Plug it in

Connect the USB-C cable. The screen lights up and shows `SmallTV` with a version number for a moment.

If the device has never been on your WiFi, it then shows a yellow **SETUP MODE** screen with three things on it:

- the name of a temporary WiFi network the device just created, normally `SmallTV-Setup`
- its password, or `(open network)` if it has none
- the address to open afterwards, `http://192.168.4.1`

Leave the device powered and on that screen while you do the next step.

If instead you get a green **CONNECTED** screen, the device is already on a WiFi network. Skip to step 4.

## 2. Join the device's own WiFi

On your phone, open the WiFi settings and join `SmallTV-Setup`. It is the device's own hotspot, not your home network.

Your phone will warn you that this network has no internet access. That is expected: the hotspot exists only so you can talk to the device. Choose to stay connected anyway. On Android, tap the notification and pick the option to keep using this network; if the phone keeps jumping back to mobile data, turn mobile data off for a minute.

A setup page usually opens by itself. If it does not, open a browser and go to `http://192.168.4.1`.

## 3. Tell it about your WiFi

You are now looking at the device's settings page, a row of tabs across the top: Status, WiFi, Display, Ticker, Clawdmeter, Radar, Home Assistant, System.

1. Open the **WiFi** tab.
2. Press **Scan networks**. After a few seconds you get a list, strongest first. Networks with a lock need a password.
3. Tap your network in the list. Its name drops into the first empty row of the table.
4. Type your WiFi password in the field next to it.
5. Press **Save & connect (reboots)**.

The device restarts, which drops the `SmallTV-Setup` hotspot. Your phone will fall back to your normal WiFi on its own, or you may need to reselect it.

Nothing appeared in the scan list? See [Troubleshooting](/smalltv-mod/manual/troubleshooting/#my-wifi-network-is-not-in-the-scan-list).

## 4. Note the address of the settings page

After the restart the screen shows a green **CONNECTED** screen for a few seconds with:

- the network it joined
- its IP address, for example `192.168.1.42`
- a name address, for example `http://smalltv-3fa2.local`

Write one of them down, or take a photo of the screen. This is how you reach the settings page from now on, from any device on your home WiFi. Either address works; the `.local` one keeps working if your router hands out a different IP later, so prefer it if your phone accepts it.

Missed the screen? Unplug the device and plug it back in to see it again.

## 5. Open the settings page and add something to show

Back on your normal home WiFi, open that address in a browser. The same tabbed page appears, now with a green dot in the top-left corner meaning the device is online.

Open the **Ticker** tab and scroll to **Tickers (rotate on screen)**:

1. Press **+ Add ticker** if there is no empty row.
2. In the first field type a symbol. `AAPL` for Apple, `BTC-USD` for bitcoin in dollars, `NESN.SW` for Nestlé in Zurich, `EURUSD=X` for the euro/dollar rate.
3. Leave the source on **Yahoo Finance** and the other fields empty.
4. Scroll to the bottom of the page and press **Save settings**.

Within a few seconds the screen shows the price, how much it moved, and a small chart. Add up to eight symbols; the screen rotates through them.

That is the whole setup. Nothing else is mandatory.

## Where to go next

- [Everyday use](/smalltv-mod/manual/everyday/) explains what you are looking at on the screen and how to get back into the settings page later.
- [Settings explained](/smalltv-mod/manual/settings/) goes through every tab and every option in plain language.
- [Troubleshooting](/smalltv-mod/manual/troubleshooting/) covers what to do when something looks wrong.
