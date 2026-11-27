---
title: HTTP API
description: Every route the device's web server answers, and the fields of the status reply.
---

The settings page talks to the device through a small JSON API over plain HTTP, and scripts can use the same routes. The routes are registered in `src/WebPortal.cpp`, and the theme routes in `src/features/theme/ThemeWeb.cpp`.

## Access rules

- With a web UI password set, every route needs HTTP digest auth (`curl --digest -u <user>:<password>`). The exceptions are `POST /api/usage`, which the usage daemon calls without credentials and which only writes the numbers on the screen, and the captive-portal probes. Only one client at a time stays logged in; see [Troubleshooting](/smalltv-mod/manual/troubleshooting/#the-settings-page-asks-for-the-password-again)
- The device answers `403 Forbidden` to a request whose `Host` header is not its IP address or hostname, and to a browser request from another site's page. Scripts and curl send no `Origin` header and are not affected. See [Troubleshooting](/smalltv-mod/manual/troubleshooting/#the-web-ui-answers-forbidden)
- The Home Assistant, notification and theme routes exist only in builds with that feature, and answer `404` elsewhere. In a build without the usage meter, `/api/usage` and `/api/activity` answer `400`

## Routes

| Method and route | What it does |
|---|---|
| `GET /` | The settings page |
| `GET /api/config` | The settings as JSON, without passwords or the WireGuard private key, plus `features` (which of `ticker`, `usage`, `radar`, `ha`, `theme`, `wireguard` and `button` the build has) and `chip` (`esp8266`, `esp32c2` or `esp32`) |
| `POST /api/config` | Applies the settings in the JSON body and saves them. Answers `{"ok":true,"reboot":<bool>}`; `reboot` is true when the WiFi list or the hostname changed, and the device then restarts. An `ha` member says that Home Assistant needs a restart. `400` for a body that is not JSON, `503` with `not enough memory` when the device has no heap for the save, which keeps the old settings |
| `GET /api/status` | Live device state, see [below](#the-status-reply) |
| `GET /api/scan` | A WiFi scan: a list of up to 25 `{"ssid","rssi","enc"}` |
| `POST /api/refresh` | Fetches every ticker now |
| `POST /api/reboot` | Restarts the device |
| `POST /api/factory` | Erases every setting and restarts in SETUP MODE |
| `POST /api/crash/clear` | Clears the last-crash record (v3.5.0 or later) |
| `GET /api/export` | Downloads `config.json`, with the WiFi passwords and the WireGuard private key in clear text |
| `POST /api/import` | Applies a `config.json` from the body, saves it and restarts |
| `GET /api/checkupdate` | Compares the running version with the newest GitHub release: `current`, `ok`, `latest`, `newer`, and `error` when the check failed |
| `POST /api/selfupdate` | Starts the GitHub self-update |
| `POST /update` | Manual firmware upload, one multipart file. Answers `OK` and restarts, or a plain-text error. On the ESP8266 an upload that cannot get its 4 KB buffer is refused at the start (v3.6.0 or later) |
| `POST /api/usage` | The usage daemon's push: `{"s":29,"sr":142,"w":4,"wr":9876,"st":"normal","ok":true}`, session and weekly percent and minutes to each reset. See [Claude usage meter](/smalltv-mod/features/usage/) |
| `POST /api/activity?state=running\|done\|idle` | The Claude Code prompt state on the usage screen (v3.4.0 or later) |
| `POST /api/notify` | A notification overlay, see [Notifications](/smalltv-mod/features/notify/) |
| `POST /api/ha/clear` | Wipes every Home Assistant screen from the device and deletes the broker's retained copies: `{"ok":true,"cleared":N,"broker":<bool>}` |
| `GET /api/themes`, `POST /api/themes/install`, `/select`, `/delete` | Theme packages, see [Theme clocks](/smalltv-mod/features/themes/#http-api) |

## The status reply

`GET /api/status` is written in 128-byte pieces as the values are read, so it goes out chunked; an HTTP/1.0 client gets plain JSON that ends when the connection closes. A client that stops reading has its connection dropped (v3.10.0 or later). Members marked "when set" are left out otherwise.

| Member | Meaning |
|---|---|
| `fw`, `version`, `variant`, `repo` | Firmware name, version (with a fork's suffix, see [Build options for forks](/smalltv-mod/reference/building/#build-options-for-forks)), the release variant such as `esp8266-lean`, and the repository URL |
| `updateMsg` | When set: the state or result of a GitHub update |
| `githubUpdate` | Only `false`, and only in a build without the GitHub self-update |
| `mode`, `connected`, `ssid`, `ip`, `rssi` | `sta` or `ap`, and the WiFi link |
| `wifiFail` | When set: why the last join failed |
| `heap`, `maxblk`, `contstk` | Free heap, the largest free block, and the free loop stack on the ESP8266, in bytes |
| `uptime`, `reset` | Seconds since boot, and the reason for the last reset |
| `synced`, `time`, `tz`, `clockFresh` | Whether NTP ever set the clock, the local time when known, the timezone, and whether the last sync is recent |
| `night`, `nightHeld` | Night mode dims the screen now; the night window is active but waits for a fresh NTP sync |
| `sun`, `bl` | Today's sunrise and sunset when the sun schedule runs, and the backlight level 0 to 255. See [Sunrise and sunset brightness](/smalltv-mod/features/sun/#seeing-what-it-does) |
| `ha` | When set: why Home Assistant is not running as set, `memory`, `safe` or `reboot`. See [Home Assistant screens](/smalltv-mod/features/ha/#switching-it-on-and-off-needs-a-restart) |
| `wg` | The tunnel: `compiledIn`, `enabled`, `up`, `held`, `badConfig`, and in a build with the client `stage`, `attempts`, `endpointIp` and `lastHandshake` (Unix time) |
| `lastCrash` | When a record exists (ESP8266, v3.5.0 or later): `reason`, `fw`, the registers `exccause`, `epc` and `addr` when the chip recorded them, and `uptime` in seconds |
| `fetchRefused`, `fetchLast` | Builds with theme clocks, once a theme data fetch has ended: fetches refused for memory since boot, and how the last one ended (`ok`, `refused` or `failed`) |
| `tlsGate` | Once an HTTPS fetch has checked the heap (ticker, Claude usage, GitHub update): `free` and `block`, the free heap and largest block that check saw, and `skipped`, the fetches it skipped for memory since boot. A fetch starts only when the free heap covers its receive buffer, 15,700 bytes for the handshake and 5,120 bytes left for the WiFi SDK, and the largest block holds 6,400 bytes (16,000 for cash.ch) |
| `tickers` | One entry per ticker: `symbol`, `valid`, `error`, `retryIn` (seconds to the next attempt) after a failure, `reason` (`not enough memory for HTTPS`) when the failure is a skip for memory, and `price`, `changePct` and `basis` (`range` or `day`) once there is data |
| `radar` | The radar's last poll: `count`, `error`, `stage`, `seenAc`, and when known `http`, `tlsRx`, `triedAgo`, `okAgo` and `url`, plus `parseError` when JSON parsing fails. See [Plane radar](/smalltv-mod/features/radar/#when-the-scope-stays-empty-on-an-esp8266) |
