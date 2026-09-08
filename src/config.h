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
#define FW_VERSION  "2.13.1"

// Project / update references (shown in the web UI; used by the GitHub self-update)
#define REPO_URL      "https://github.com/giovi321/smalltv-mod"
#define REPO_OWNER    "giovi321"
#define REPO_NAME     "smalltv-mod"
// Release asset the GitHub self-updater pulls, and the short variant name shown
// in the web UI. One app image per target; the ESP8266 has two, standard and
// lean, so a device keeps its own variant across a self-update instead of
// silently gaining or losing features.
#if defined(SMALLTV_ESP32C2)
  #define UPDATE_ASSET "smalltv-mod-firmware-c2.bin"
  #define FW_VARIANT   "c2"
#elif defined(SMALLTV_ESP32_PRO)
  #define UPDATE_ASSET "smalltv-mod-firmware-esp32-pro.bin"
  #define FW_VARIANT   "esp32-pro"
#elif defined(SMALLTV_ESP32)
  #define UPDATE_ASSET "smalltv-mod-firmware-esp32.bin"
  #define FW_VARIANT   "esp32"
#elif defined(SMALLTV_LEAN)
  #define UPDATE_ASSET "smalltv-mod-firmware-lean.bin"
  #define FW_VARIANT   "esp8266-lean"
#else
  #define UPDATE_ASSET "smalltv-mod-firmware.bin"
  #define FW_VARIANT   "esp8266"
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
#elif defined(SMALLTV_ESP32_PRO)
  #include "board_esp32_pro.h"
#elif defined(SMALLTV_ESP32)
  #include "board_esp32.h"
#else
  #include "board_esp8266.h"
#endif

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// Panel RAM offsets per rotation pair (Arduino_GFX: offset1 -> rotation 0/1,
// offset2 -> rotation 2/3). The ST7789(V) is a 240x320 controller but the
// 1.54" glass only wires RAM rows 0-239, leaving an 80-row dead band. With
// the MADCTL MY bit set (rotation 2/3) the scan direction reverses into that
// dead band, so the row offset must jump to 80 or the image slides 80 px off
// the glass (content pushed to the top at 180°). #ifndef so a board header
// can override if a variant turns up with a 240x280 panel (would need 40).
#ifndef TFT_COL_OFFSET1
#define TFT_COL_OFFSET1 0
#endif
#ifndef TFT_ROW_OFFSET1
#define TFT_ROW_OFFSET1 0
#endif
#ifndef TFT_COL_OFFSET2
#define TFT_COL_OFFSET2 0
#endif
#ifndef TFT_ROW_OFFSET2
#define TFT_ROW_OFFSET2 80
#endif

// ---------------------------------------------------------------------------
// Limits (bound RAM usage on the ESP8266)
// ---------------------------------------------------------------------------
#define MAX_SYMBOLS       8    // max tickers in the rotation
#define MAX_SYMBOL_LEN   24    // e.g. "BTC-USD", cash.ch key "123456789-246-333"
#define MAX_WIFI_NETS     4    // saved WiFi networks; strongest visible wins at boot
#define MAX_NAME_LEN     20    // friendly name shown on screen
#define MAX_SPARK_POINTS 60    // sparkline samples kept per symbol
#define MAX_URL_LEN     200    // webhook base URL

// ---------------------------------------------------------------------------
// Web UI password (off by default). Digest auth, so the password itself is
// never sent over the wire even though the UI is plain HTTP.
// ---------------------------------------------------------------------------
#define MAX_AUTH_USER_LEN 32
#define MAX_AUTH_PASS_LEN 64
#define DEFAULT_AUTH_USER "admin"
#define AUTH_REALM        "SmallTV"

// ---------------------------------------------------------------------------
// WireGuard client. Compiled only where the image has room for it: see
// SMALLTV_WIREGUARD in platformio.ini, which sets it for the ESP32-C2 and the
// 8 MB SmallTV Pro. Reaches the device from outside the LAN without forwarding
// its plain-HTTP port to the internet. The ESP8266 has neither the flash nor
// the heap for it.
// ---------------------------------------------------------------------------
#define MAX_WG_KEY_LEN    48   // base64 x25519 key is 44 chars + NUL, with headroom
#define MAX_WG_HOST_LEN   64   // endpoint hostname or IP
#define MAX_WG_ADDR_LEN   24   // tunnel address without the prefix
#define MAX_WG_ALLOWED_LEN 80  // comma-separated allowed-IPs list
#define DEFAULT_WG_PORT        51820
#define DEFAULT_WG_KEEPALIVE      25   // seconds; 0 = off. 25 survives most NATs

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
#define MODE_NOTIFY    4             // transient overlay: armed over HTTP, never persisted
#define MODE_HA        5             // Home Assistant screens pushed over MQTT
#define DEFAULT_MODE MODE_STOCKS
#define DEFAULT_CAROUSEL_SEC 30      // per-mode dwell in carousel

// Full-screen attention overlay (POST /api/notify), in seconds.
#define NOTIFY_TTL_DEFAULT_SEC  20
#define NOTIFY_TTL_MIN_SEC       2
#define NOTIFY_TTL_MAX_SEC     120

// ---------------------------------------------------------------------------
// Home Assistant screens (MODE_HA, features/ha): full screens pushed over MQTT
// as retained JSON draw lists, one per slot, on smalltv/<hostname>/screen/<slot>.
// The C2 counts as an ESP32 here (the docs group them); the ESP8266 column of
// the limits table is for the original GeekMagic unit only, where heap is the
// binding constraint. MQTT_MAX_PACKET_SIZE is NOT set here: it has to reach
// PubSubClient's own translation units, so it is a -D in each env's
// build_flags in platformio.ini (2048 on the ESP32 family, 768 on ESP8266).
// ---------------------------------------------------------------------------
#if defined(SMALLTV_ESP32) || defined(SMALLTV_ESP32C2) || defined(SMALLTV_ESP32_PRO)
  #define HA_MAX_SCREENS   8      // slots kept; carousel order = slot name order
  #define HA_MAX_PRIMS     48     // draw primitives kept per screen
  #define HA_TEXT_POOL     2048   // per-screen pool backing the text primitives
  #define HA_BITMAP_POOL   2048   // per-screen pool of decoded 1-bit bitmap bytes
  #define HA_BITMAP_MAX_DIM 48    // bitmap w/h cap: one bitmap is <= 288 B
#else
  #define HA_MAX_SCREENS   4
  #define HA_MAX_PRIMS     24
  #define HA_TEXT_POOL     512    // the 768 B MQTT payload bounds text anyway
  #define HA_BITMAP_POOL   512
  #define HA_BITMAP_MAX_DIM 32    // one bitmap is <= 128 B (256 hex chars)
#endif
#define HA_MAX_TEXT        65     // one text value: 64 chars + NUL
#define HA_SLOT_LEN        25     // slot (topic suffix) cap: 24 chars + NUL
#define HA_TTL_MAX_SEC     604800UL  // ttl clamp: 7 days (keeps millis() math sane)
#define HA_PERSIST_DEBOUNCE_MS 2000UL  // /ha_screens.json write delay after a change

// Broker settings slice (Settings.h): field caps + defaults.
#define MAX_HA_HOST_LEN        64
#define MAX_HA_USER_LEN        32
#define MAX_HA_PASS_LEN        32
#define DEFAULT_HA_BROKER_PORT 1883
#define DEFAULT_HA_DWELL_SEC   15
#define HA_DWELL_MIN_SEC        3
#define HA_DWELL_MAX_SEC      300

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
#ifndef WITH_NOTIFY
#define WITH_NOTIFY 1
#endif
#ifndef WITH_HA
#define WITH_HA 1
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
#define SRC_GHUB     3   // static JSON read from a repo's data branch (see below)
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

// GitHub source (SRC_GHUB): static quote JSON published to a git repo's `data`
// branch and read from raw.githubusercontent.com, which — unlike cash.ch —
// still accepts the ESP8266's static-RSA handshake (the same one GitHub
// self-update and Yahoo use). The file is the same JSON the webhook parser
// accepts, and the symbol is the cash.ch listing key. You publish the files
// yourself from a fork: .github/scripts/fetch-quotes.mjs + quotes-config.json
// are the example fetcher and symbol list, and the docs
// (reference/data-sources) show an example scheduled workflow that pushes them
// to a `data` branch — point REPO_OWNER/REPO_NAME at that fork. raw sends a
// ~4 KB certificate record and does not negotiate MFLN, so this path uses a
// larger TLS buffer.
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
//     2 = adsb.lol opendata, same JSON shape, also direct over HTTPS (no key)
// ---------------------------------------------------------------------------
#define RADAR_SRC_ADSBFI   0
#define RADAR_SRC_WEBHOOK  1
#define RADAR_SRC_ADSBLOL  2

// Both free open-data feeds return the same {"ac":[...]} shape and take the
// same lat/lon/dist-in-nautical-miles path; only host and prefix differ.
//   adsb.fi:  /api/v3/lat/{lat}/lon/{lon}/dist/{nm}
//   adsb.lol: /v2/lat/{lat}/lon/{lon}/dist/{nm}
// Public rate limit is ~1 req/s on both; neither needs an API key.
#define ADSB_FI_HOST     "opendata.adsb.fi"
#define ADSB_FI_PATH     "/api/v3/lat/"
#define ADSB_LOL_HOST    "api.adsb.lol"
#define ADSB_LOL_PATH    "/v2/lat/"
#define ADSB_USER_AGENT  "Mozilla/5.0 (SmallTV)"

// Default direct provider. adsb.fi sits behind Cloudflare, which does not
// negotiate the TLS max_fragment_length extension and sends records larger
// than BearSSL's fallback 4 KB buffer, so the ESP8266 cannot read a busy
// response; adsb.lol still honours MFLN and keeps the TLS footprint tiny.
// The ESP32 uses mbedTLS with dynamic buffers and is not affected either way.
#if defined(SMALLTV_ESP8266)
  #define DEFAULT_RADAR_SRC  RADAR_SRC_ADSBLOL
#else
  #define DEFAULT_RADAR_SRC  RADAR_SRC_ADSBFI
#endif

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
// Per-symbol retry after a failed or skipped fetch: the first retry comes after
// TICKER_RETRY_SEC and then doubles (12s, 24s, 48s, 96s) for TICKER_RETRY_MAX
// steps, after which the symbol settles at the poll interval and keeps retrying
// there. A retry is never scheduled further out than the poll interval.
#define TICKER_RETRY_SEC       12
#define TICKER_RETRY_MAX        4
#define DEFAULT_ROTATE_SEC    10             // how long each symbol is shown
#define DEFAULT_RANGE        "1d"            // chart timeframe (e.g. 1d/5d/1mo/1y)
#define DEFAULT_POINTS        48             // sparkline points requested
#define DEFAULT_BRIGHTNESS    90             // 0..100 %
#define DEFAULT_HTTP_TIMEOUT  8000           // ms per request

// --- Panel colour correction (device-wide) ---
// Panels differ between (and within) the SmallTV variants: white balance drifts
// and some controllers have red and blue swapped. AUTO keeps the board header's
// TFT_BGR default; RGB/BGR force the MADCTL colour-order bit either way.
#define COLOR_ORDER_AUTO   0
#define COLOR_ORDER_RGB    1
#define COLOR_ORDER_BGR    2
#define DEFAULT_COLOR_ORDER  COLOR_ORDER_AUTO
#define DEFAULT_COLOR_INVERT false
#define DEFAULT_COLOR_GAIN   100     // percent per channel; 50..150 accepted
#define MIN_COLOR_GAIN        50
#define MAX_COLOR_GAIN       150

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
