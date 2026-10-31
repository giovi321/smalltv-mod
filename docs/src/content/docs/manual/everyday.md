---
title: Everyday use
description: What the screens mean, how to reach the settings page again, and what happens after a power cut.
---

Once the device is on your WiFi it needs no attention. It fetches its data by itself, redraws the screen, and comes back on its own after a power cut. This page explains what you are looking at and the few things you may want to do from time to time.

## Reading the ticker screen

One symbol at a time, swapping to the next every ten seconds by default.

- The big number is the current price, with its currency in front. Non-dollar currencies appear as a three-letter code (`CHF 79.73`, `EUR 12.40`) because the built-in font has no euro or franc symbol.
- Below it, how much the price moved and by what percentage, with an arrow. Green up and red down by default, and you can swap the colours.
- The small chart is the price over the selected timeframe, one day by default.
- The dots at the bottom, if enabled, say how many symbols are in the rotation and which one you are on.

Two things you may see instead of a price:

- `loading...` under a symbol, right after a restart or after you added it. It should turn into a price within a few seconds.
- `fetch error`, meaning the device asked for that symbol and got nothing usable back. Usually the symbol is misspelled or does not exist at that data source. See [Troubleshooting](/smalltv-mod/manual/troubleshooting/#a-symbol-shows-fetch-error-or-stays-on-loading).

A small red dot in the top-left corner means the last refresh failed and the numbers on screen are the last good ones, so they are older than they look. It clears itself when a refresh succeeds. The device keeps trying on its own: a symbol that fails comes back after 12 seconds, then 24, 48, 96, and after that at the normal refresh interval for as long as it keeps failing, so a ticker that broke while you were out fixes itself as soon as the data comes back. The Ticker tab lists which symbols are failing and when each is next due.

### The portfolio page

If you filled in a quantity and a purchase price for a symbol, that symbol's page also shows your gain or loss on it, and an extra `Portfolio` page joins the rotation listing every position and a total per currency. Totals are kept separately per currency and are never converted between them. Turn both off with the "Position P/L & portfolio page" tick in the Ticker tab.

## Reading the Claude usage screen

This mode only shows numbers while a program on your PC is sending them. See [Claude usage meter](/smalltv-mod/features/usage/) for that part.

- With data flowing: a small mascot, your 5-hour and 7-day usage as percentages with bars that go from green through amber to red, and how long until each window resets. On a plan without a 5-hour limit, the Clawdmeter tab can hide the 5-hour card, leaving one larger 7-day card.
- Without data: an animated mascot, and `waiting...` or `daemon error` on the first run. This is the normal state when your PC is asleep or the program is not running. It flips back to the numbers by itself when data returns.

## Reading the radar screen

A scope centred on the location you entered, with the range rings you picked, `N` marking north at the top, and a marker for home in the middle. Aircraft are triangles pointing the way they are flying, with an optional line for speed and a label with the callsign or altitude. Traffic outside the outer ring shows as dots on the rim. Airports you added appear as small markers.

If the screen says `Set home location`, the latitude and longitude in the Radar tab are still empty.

## Carousel

Set the mode to **Carousel** and the device cycles through whichever modes you ticked, staying on each for the number of seconds you set. It keeps fetching data for the mode on screen only. **Theme clock** is unticked by default; tick it once a theme is installed and selected.

## Getting back into the settings page

Open the address from the CONNECTED screen in any browser on the same WiFi:

- `http://smalltv-xxxx.local`, the name address, which survives an IP change
- or the plain IP, for example `http://192.168.1.42`

Forgot both? Any of these works:

- Unplug the device and plug it back in. The CONNECTED screen shows both addresses for a few seconds at boot.
- Look at the list of connected devices in your router's admin page. The device appears under its hostname.

The phone or computer has to be on the same WiFi network as the device. This does not work over mobile data, and often does not work across a guest network.

## Changing what it shows

Everything is in the settings page, and every tab is described in [Settings explained](/smalltv-mod/manual/settings/). The two things worth knowing up front:

- There is one **Save settings** button at the very bottom of the page, and it saves every tab at once. Changing something and closing the browser without pressing it changes nothing.
- Most changes take effect within a second or two. Changing the WiFi network or the device name restarts the device, which takes a few seconds.

## Brightness, colour, and the night schedule

Brightness sits in the Display tab, at 90 percent out of the box. If your unit has a light sensor, "Auto-brightness" lets it follow the room instead.

The "Colour" card below it is for when the screen's colours look off. These devices do not all carry the same panel, so one unit can look warmer or greener than another running the same firmware. Pull a channel down with the Red, Green, and Blue sliders until it looks right, and press "Reset to 100%" to go back to the untouched image. Two rarer faults have their own controls in the same card: red and blue coming out swapped is the "Colour order" setting, and a screen that looks like a photographic negative is "Invert the panel". Colour changes show up on the screen as soon as you save, so adjust with the device in front of you.

The "Clock & night mode" card can dim the screen, or switch the backlight off entirely, between two times you choose. Pick your timezone from the list first: the device has no clock of its own and gets the time from the internet, and the schedule needs to know which timezone your times are in. Daylight saving is handled for you. The device stays online and reachable through the night, it just stops lighting the screen.

Two things behave the way they do on purpose:

- Right after a restart the screen may show normal brightness for a few seconds even inside your night window, until the time has been fetched again.
- If the device cannot reach the internet to check the time, it leaves the screen on rather than guessing, and keeps retrying. A device that never gets the time is never left dark.

## After a power cut, and moving house

Settings live in the device's own memory, so nothing is lost when it loses power. It reconnects and picks up where it was.

You can save up to four WiFi networks in the WiFi tab, and at each start the device joins the strongest one it can see. Save the new network before you move and it comes up on its own at the other end. If none of the saved networks is reachable, the device falls back to the yellow SETUP MODE screen and its own hotspot, so you can always point it at a new network as in [Quick start](/smalltv-mod/manual/quick-start/). While the hotspot is up and nobody is connected to it, it keeps retrying the saved networks every 90 seconds and restarts once one of them answers.

## Keeping it up to date

New versions are published on GitHub and the device can install them itself.

1. Open the **System** tab. The installed version is at the top.
2. Press **Check for latest**.
3. If a newer version exists, press **Update now** and confirm.

The download takes a minute or two. Leave the device powered until it comes back, and expect the screen to blank and the device to restart, twice on the ESP8266 model. If the download fails, the device just carries on with the version it has and the System tab says why.

Two exceptions worth knowing:

- On the older ESP8266 model still running firmware 2.6.1 or earlier, the self-updater is broken and cannot fix itself. Update that device by hand once, as described in [Flashing](/smalltv-mod/getting-started/flashing/#after-the-first-flash). Afterwards the button works.
- Updating never erases your settings.

## Care

- The USB-C port is there to power the device. It does not have to be plugged into a computer.
- The screen is a small IPS panel with no touch. Every control is in the web page.
- There are no power buttons on the case (the SmallTV Pro has a touch button, controlled by software). A restart means unplugging it, or pressing Reboot in the System tab.
