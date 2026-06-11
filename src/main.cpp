// smalltv-mod — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Two modes, picked in the web UI:
//   - Stock / crypto ticker: price, % change and sparkline (Yahoo or a webhook),
//     rotating through several symbols.
//   - Claude usage: an animated pixel mascot + 5h/7d usage bars, fed over WiFi by
//     the local daemon (see daemon/).
// Configured entirely from a built-in web UI, with OTA firmware updates.
//
// License: WTFPL
#include <Arduino.h>
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Display.h"
#include "StockClient.h"
#include "UsageClient.h"
#include "Mascot.h"
#include "WebPortal.h"

static Settings g_settings;

// rotation / render state (stock mode)
static uint8_t  g_curPage = 0;
static uint32_t g_lastRotate = 0;
static uint32_t g_renderedLastOk = 0xFFFFFFFF;
static bool     g_renderedError = false;
static bool     g_needRender = true;
static uint32_t g_lastAutoBr = 0;

// usage-mode render state
static uint32_t g_usageSampled = 0;          // lastOkMs already fed to the mascot tracker
static uint32_t g_usageRenderedOk = 0xFFFFFFFF;
static bool     g_showingMascot = false;

static void bootProgress(const char* msg) {
  displayBootMessage("SmallTV", msg);
}

// Called by the web portal after settings are applied: re-init the usage client
// and force a fresh repaint so a mode/URL change takes effect immediately.
void appInvalidate() {
  g_needRender = true;
  g_showingMascot = false;
  g_usageRenderedOk = 0xFFFFFFFF;
  g_renderedLastOk = 0xFFFFFFFF;
  usageInit(g_settings);
  usageForceRefresh();
}

static void renderCurrent() {
  uint8_t n = stocksCount();
  if (n == 0) {
    displayMessage("No tickers", netIP().c_str(), 0xFFE0);
    return;
  }
  if (g_settings.source == SRC_WEBHOOK && g_settings.webhookUrl.length() < 8) {
    displayMessage("Set webhook", netIP().c_str(), 0xFFE0);
    return;
  }
  if (g_curPage >= n) g_curPage = 0;
  displayStock(stockAt(g_curPage), g_curPage, n, g_settings);
}

// --- stock ticker mode: fetch, rotate, render ------------------------------
static void serviceStockMode() {
  stocksService(g_settings);

  uint8_t n = stocksCount();

  // Rotate to next ticker
  if (n > 1 && millis() - g_lastRotate >= (uint32_t)g_settings.rotateSec * 1000UL) {
    g_curPage = (g_curPage + 1) % n;
    g_lastRotate = millis();
    g_needRender = true;
  }

  // Re-render when the displayed ticker's data changed
  if (n > 0 && g_curPage < n) {
    const StockData& d = stockAt(g_curPage);
    if (d.lastOkMs != g_renderedLastOk || d.error != g_renderedError) {
      g_needRender = true;
      g_renderedLastOk = d.lastOkMs;
      g_renderedError = d.error;
    }
  }

  if (g_needRender) {
    renderCurrent();
    g_needRender = false;
  }
}

// --- Claude usage mode: stats when data is flowing, idle mascot otherwise ----
static void serviceUsageMode() {
  if (g_settings.usageUrl.length() < 8) {
    if (g_needRender) { displayMessage("Set usage URL", netIP().c_str(), 0xFFE0); g_needRender = false; }
    return;
  }

  usageService(g_settings);
  const UsageData& u = usageGet();

  // Feed the burn-rate tracker once per fresh reading (drives the mascot's mood).
  if (u.valid && u.lastOkMs != g_usageSampled) {
    g_usageSampled = u.lastOkMs;
    mascotSample(u.sessionPct);
  }

  // Considered stale after ~2 missed polls (plus a grace) — then show the animation.
  uint32_t staleMs = (uint32_t)g_settings.pollSec * 1000UL * 2UL + USAGE_STALE_GRACE_MS;

  if (usageFresh(staleMs)) {
    if (g_showingMascot) { g_showingMascot = false; g_needRender = true; }
    if (u.lastOkMs != g_usageRenderedOk) { g_usageRenderedOk = u.lastOkMs; g_needRender = true; }
    if (g_needRender) { displayUsage(u, g_settings); g_needRender = false; }
  } else {
    if (!g_showingMascot) {
      g_showingMascot = true;
      g_usageRenderedOk = 0xFFFFFFFF;
      mascotReset();
      displayMascot(mascotCells(), mascotPalette(), /*restart=*/true);
    } else if (mascotTick()) {
      displayMascot(mascotCells(), mascotPalette(), /*restart=*/false);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);

  Serial.println("[boot] display");
  displayBegin(g_settings);
  displayBootMessage("SmallTV", FW_VERSION);

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  Serial.println("[boot] web");
  webPortalBegin(g_settings);
  Serial.println("[boot] stocks");
  stocksInit(g_settings);
  Serial.println("[boot] usage");
  usageInit(g_settings);
  mascotInit();
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    displayApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(),
                  netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    displayStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
    delay(3500);
    g_needRender = true;
    g_lastRotate = millis();
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: fetch + render (per selected mode) ---

  // Auto-brightness (optional LDR on A0) — applies to either mode.
  if (g_settings.autoBrightness && millis() - g_lastAutoBr > 2000) {
    g_lastAutoBr = millis();
    int raw = analogRead(LDR_PIN);              // 0..1023
    uint8_t pct = (uint8_t)constrain(raw * 100 / 1023, 5, 100);
    displaySetBrightness(pct, g_settings.backlightInverted);
  }

  if (g_settings.mode == MODE_USAGE) serviceUsageMode();
  else                               serviceStockMode();

  delay(5);
}
