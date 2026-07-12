// config.h — compile-time constants for smalltv-mod
//
// Hardware: three board variants, all a 1.54" 240x240 ST7789 IPS panel:
//   - Original GeekMagic SmallTV: ESP-12F (ESP8266)      [board_esp8266.h]
//   - Knockoff SmallTV:           ESP32-C2 / ESP8684      [board_esp32c2.h]
//   - NMMiner NM-TV-154:          classic ESP32 (WROOM-32E) [board_esp32.h]
// The board-specific pin map + panel quirks live in the board headers, selected
// below by the build-time target macro. Everything else here is shared.
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "smalltv-mod"
#define FW_VERSION  "2.8.2"

// Project / update references (shown in the web UI; used by the GitHub self-update)
#define REPO_URL      "https://github.com/giovi321/smalltv-mod"
#define REPO_OWNER    "giovi321"
#define REPO_NAME     "smalltv-mod"
// Release asset the GitHub self-updater pulls — one app image per target.
#if defined(SMALLTV_ESP32C2)
  #define UPDATE_ASSET "smalltv-mod-firmware-c2.bin"
#elif defined(SMALLTV_ESP32)
  #define UPDATE_ASSET "smalltv-mod-firmware-esp32.bin"
#else
  #define UPDATE_ASSET "smalltv-mod-firmware.bin"
#endif
#define GH_API_HOST   "api.github.com"
#define DAEMON_URL    "https://github.com/giovi321/clawdmeter-daemon"

// ---------------------------------------------------------------------------
// Display wiring + panel quirks — board-specific, pulled from the right header.
// Provides TFT_SCLK/MOSI/DC/RST/CS/BL, TFT_BGR, TFT_BL_DEFAULT_INVERTED,
// HAS_LDR/LDR_PIN/ADC_MAX. Both panels are 1.54" 240x240 ST7789 IPS.
// ---------------------------------------------------------------------------
#if defined(SMALLTV_ESP32C2)
  #include "board_esp32c2.h"
#elif defined(SMALLTV_ESP32)
  #include "board_esp32.h"
#else
  #include "board_esp8266.h"
#endif

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ---------------------------------------------------------------------------
// Limits (bound RAM usage on the ESP8266)
// ---------------------------------------------------------------------------
#define MAX_SYMBOLS       8    // max tickers in the rotation
#define MAX_SYMBOL_LEN   24    // e.g. "BTC-USD", cash.ch key "147478611-246-333"
#define MAX_WIFI_NETS     4    // saved WiFi networks; strongest visible wins at boot
#define MAX_NAME_LEN     20    // friendly name shown on screen
#define MAX_SPARK_POINTS 60    // sparkline samples kept per symbol
#define MAX_URL_LEN     200    // webhook base URL

// ---------------------------------------------------------------------------
// Display mode — what the device shows
//   0 = stock / crypto ticker (per-symbol source, see SRC_* below)
//   1 = Claude usage meter (mascot + 5h/7d usage bars, fed by the daemon/)
//   2 = plane radar
//   3 = carousel: rotate through the ticked features on a timer
// ---------------------------------------------------------------------------
#define MODE_STOCKS    0
#define MODE_USAGE     1
#define MODE_RADAR     2
#define MODE_CAROUSEL  3
#define DEFAULT_MODE MODE_STOCKS
#define DEFAULT_CAROUSEL_SEC 30      // per-mode dwell in carousel

// ---------------------------------------------------------------------------
// Compile-time feature toggles. All shipping features are on by default; a lean
// build drops one by setting e.g. -D WITH_RADAR=0 in a PlatformIO env, which
// omits that feature's module from the registry and its web UI section.
// (WITH_RADAR ships off until the radar module lands.)
// ---------------------------------------------------------------------------
#ifndef WITH_TICKER
#define WITH_TICKER 1
#endif
#ifndef WITH_USAGE
#define WITH_USAGE 1
#endif
#ifndef WITH_RADAR
#define WITH_RADAR 1
#endif

// Claude usage mode: once data stops arriving for this long (PC asleep, daemon
// stopped, network down) the screen switches from the stats to the idle mascot
// animation. Effective timeout also scales with the poll period (see main.cpp).
#define USAGE_STALE_GRACE_MS  20000UL

// ---------------------------------------------------------------------------
// Data source (stock mode)
//   0 = custom webhook (n8n / Node-RED / your own HTTP endpoint)
//   1 = Yahoo Finance, fetched directly by the device (no backend needed)
//   2 = cash.ch, fetched directly by the device (Swiss instruments, incl.
//       off-exchange structured products that Yahoo doesn't carry)
// ---------------------------------------------------------------------------
#define SRC_WEBHOOK  0
#define SRC_YAHOO    1
#define SRC_CASH     2
#define SRC_GHUB     3   // static JSON published to the repo's data branch (see below)
#define DEFAULT_SOURCE  SRC_YAHOO            // works out of the box, no server

// Yahoo Finance public chart endpoint. A browser-like User-Agent is required —
// requests with an empty UA are rejected with HTTP 429. TLS records from Yahoo
// are <=~1.3 KB, so the 4 KB BearSSL receive buffer in StockClient is plenty.
// query1/query2 are interchangeable mirrors; we fall back to the second on a
// transient failure (a single back-to-back HTTPS fetch occasionally drops).
#define YAHOO_CHART_HOST1 "query1.finance.yahoo.com"
#define YAHOO_CHART_HOST2 "query2.finance.yahoo.com"
#define YAHOO_CHART_PATH  "/v8/finance/chart/"
#define YAHOO_USER_AGENT  "Mozilla/5.0 (SmallTV)"

// cash.ch public GraphQL endpoint. The device sends two small hand-written
// GraphQL queries per symbol as plain GETs (?query=...): a ~200 B quote and a
// slim daily-close series for the sparkline. No API key, no cookies, no
// required headers. The symbol is the cash.ch listing key
// `valor-marketId-currencyId` (see the docs for how to find it).
// cash.ch's CDN requires ECDHE. The ESP32 targets (mbedTLS) do this easily. The
// ESP8266 (BearSSL) can too, but the handshake is memory-tight, so the cash
// path is shaped to fit: only cash.ch is offered ECDHE (Yahoo and the GitHub
// source are pinned to the cheap static-RSA suites), the connection uses 512 B
// buffers + TLS session resumption, and StockClient skips a fetch unless a
// large enough contiguous heap block is free. The GitHub source below is a
// zero-crash fallback if a device ever proves too tight for the direct path.

// GitHub source (SRC_GHUB): a scheduled workflow (.github/workflows/quotes.yml)
// fetches cash.ch server-side and publishes one JSON file per listing key to
// the repo's `data` branch. The device reads it from raw.githubusercontent.com,
// which — unlike cash.ch — still accepts the ESP8266's static-RSA handshake
// (the same one GitHub self-update and Yahoo use). The file is the same JSON
// the webhook parser accepts. The symbol is the cash.ch listing key; only keys
// listed in quotes-config.json are published. raw sends a ~4 KB certificate
// record and does not negotiate MFLN, so this path uses a larger TLS buffer.
#define GH_QUOTES_BASE "https://raw.githubusercontent.com/" REPO_OWNER "/" REPO_NAME "/data/quotes/"
#define GH_QUOTES_RXBUF 5120
#define CASH_GQL_HOST   "www.cash.ch"
#define CASH_GQL_PATH   "/_/api/graphql/prod"
#define CASH_USER_AGENT "Mozilla/5.0 (SmallTV)"

// ---------------------------------------------------------------------------
// Plane radar (MODE_RADAR)
//   Data source (radar's own selector, independent of the stock one):
//     0 = adsb.fi opendata, fetched directly by the device over HTTPS (no key)
//     1 = custom webhook (a LAN proxy that pre-filters — robust on the ESP8266)
// ---------------------------------------------------------------------------
#define RADAR_SRC_DIRECT   0
#define RADAR_SRC_WEBHOOK  1
#define DEFAULT_RADAR_SRC  RADAR_SRC_DIRECT

// adsb.fi free open-data endpoint (no API key; public rate limit ~1 req/s).
// Full path: /api/v3/lat/{lat}/lon/{lon}/dist/{nm}
#define ADSB_HOST        "opendata.adsb.fi"
#define ADSB_PATH        "/api/v3/lat/"
#define ADSB_USER_AGENT  "Mozilla/5.0 (SmallTV)"

// Bound RAM: nearest N aircraft kept/drawn, and a few home-area airports.
#define MAX_AIRCRAFT     24
#define MAX_AIRPORTS      6
#define MAX_ICAO_LEN      8      // ICAO ident + NUL (e.g. "LSZH")

// Defaults (lat/lon 0,0 is the "not set yet" sentinel -> shows a prompt).
#define DEFAULT_RADAR_LAT       0.0f
#define DEFAULT_RADAR_LON       0.0f
#define DEFAULT_RADAR_RANGE_KM  20
#define DEFAULT_RADAR_POLL_SEC  10     // >=3 keeps us under the 1 req/s limit

// ---------------------------------------------------------------------------
// Defaults (used on first boot / factory reset)
// ---------------------------------------------------------------------------
#define DEFAULT_AP_SSID      "SmallTV-Setup"
#define DEFAULT_AP_PASS      ""              // empty => open AP
#define DEFAULT_HOSTNAME     "smalltv"
#define DEFAULT_POLL_SEC      120            // how often to refresh data
#define TICKER_RETRY_SEC       12            // fast retry after a failed/skipped fetch
#define TICKER_RETRY_MAX        4            // consecutive fast retries before backing off
#define DEFAULT_ROTATE_SEC    10             // how long each symbol is shown
#define DEFAULT_RANGE        "1d"            // chart timeframe (e.g. 1d/5d/1mo/1y)
#define DEFAULT_POINTS        48             // sparkline points requested
#define DEFAULT_BRIGHTNESS    90             // 0..100 %
#define DEFAULT_HTTP_TIMEOUT  8000           // ms per request

// --- Clock / night mode (device-wide) ---
#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.nist.gov"
#define DEFAULT_TZ_NAME         ""        // IANA display name; empty = UTC
#define DEFAULT_TZ_POSIX        "UTC0"    // POSIX TZ rule the device feeds SNTP
#define DEFAULT_NIGHT_ENABLED   false
#define DEFAULT_NIGHT_START_MIN 1320      // 22:00
#define DEFAULT_NIGHT_END_MIN   420       // 07:00
#define DEFAULT_NIGHT_LEVEL     0         // 0..100, 0 = backlight fully off

// Night-mode NTP trust: only ENTER night mode when the clock was confirmed by a
// successful NTP sync within NIGHT_NTP_TRUST_MS (else we assume the clock may be
// wrong and keep the screen on). While inside the window but unconfirmed, re-arm
// SNTP every NIGHT_NTP_RESYNC_MS until a fresh sync lands or the window ends
// (morning). Once night mode has switched on, it stays on until the window ends.
#define NIGHT_NTP_TRUST_MS      300000UL  // 5 min: max age of the sync that unlocks night
#define NIGHT_NTP_RESYNC_MS      30000UL  // re-sync attempt cadence while held off
