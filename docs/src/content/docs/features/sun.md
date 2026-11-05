---
title: Sunrise and sunset brightness
description: Let the screen follow the sun. It fades down at sunset, stays dim through the night, and fades back up at sunrise, worked out on the device from your location.
---

The screen can follow the sun instead of the clock. It stays at the normal brightness by day, fades down after sunset, rests at a low level through the night, and fades back up after sunrise. The device works out sunrise and sunset itself from your location and the date. Nothing is fetched for it.

It lives in the **Clock & night mode** card of the **Display** tab. It is off by default. It needs firmware v3.4.0 or later.

## Set the device location

The schedule needs to know where the device is. Enter a latitude and longitude in decimal degrees in the **Device location** fields (north and east are positive). Four decimals are kept. For Amsterdam that is `52.3676` and `4.9041`.

You can also type a place name into the search box and pick a result. The search runs in your browser against the free Open-Meteo place search, and only the name you typed is sent. The device itself never sends the name anywhere. The search fills the two fields. Check them, then save.

**0 and 0 mean "not set".** That is the default. With no location the schedule cannot run, and the screen keeps its normal brightness.

The same location is used by the [plane radar](/smalltv-mod/features/radar/) unless you give the radar its own.

The times are worked out for the timezone chosen in the same card, daylight saving included. Pick the right timezone, or sunrise will land at the wrong hour. Accuracy is about one minute, except near the poles.

## Turn it on

Tick **Follow sunrise and sunset**, set the options below and save.

- **Lowest brightness** is the level at night, from 0 to 100 percent. The default is 5. At 0 the backlight goes fully off at night. The daytime level is the normal brightness slider in the same tab.
- **Sunrise fade** and **Sunset fade** are how many minutes each change takes, from 0 to 120. The default is 60 for both. The fade starts at sunrise and at sunset, not before. A fade of 0 jumps at once. The steps follow how the eye sees brightness, so they look even, not bunched at one end. If the sunset fade is longer than the night, it ends at the next sunrise.

The sun schedule and auto-brightness (the light sensor on boards that have one) never run together. Switching one on switches the other off, in the web UI and also when a save arrives through the API.

## When night mode is also on

You can have both night mode and the sun schedule. **When they overlap** says which one wins while the night-mode window is active:

- **Night mode first** (the default): inside the night window, the night-mode level is used. Outside it, the sun schedule is used.
- **Sun schedule first**: the sun schedule is used all the time, and the night-mode window changes nothing.
- **The darker of the two**: the lower of the two levels is used.

The button wake on boards with a touch button still lifts the screen to the slider level for a few seconds, whatever is chosen here.

## The polar cases

Near the poles the sun may not rise or set on some days.

- **Polar day**: the sun stays up all day. The screen stays at the normal brightness.
- **Polar night**: the sun stays down all day. The screen stays at the lowest brightness.

The device decides this for each day from where the sun stands at noon on that day.

## Until the clock is set

The device has no clock of its own. It gets the time over NTP, from the internet. Turning the sun schedule on starts that, as night mode does.

**Until the first NTP sync after a boot, the schedule does not run, and the screen uses the normal brightness from the slider.** The same is true if there is no location. After a restart you may see the normal brightness for a few seconds before the schedule takes over. If the device cannot reach the internet, it stays on the slider.

## Seeing what it does

Under the schedule, the card shows today's sunrise and sunset and the current brightness. `GET /api/status` has two members for this:

- `sun`: today's times as `"07:12-17:03"`, or `"polar day"` or `"polar night"`. It is left out when the schedule cannot run (off, no location, or no clock yet).
- `bl`: the backlight level now, from 0 to 255. It reads `-1` until the first level has been written after boot.
