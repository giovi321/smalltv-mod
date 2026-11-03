// smalltv-mod — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Display features are self-contained DisplayMode implementations (see Mode.h),
// selected at build time and dispatched from the registry below:
//   - Ticker (features/ticker):  stock/crypto price, % change, sparkline.
//   - Usage  (features/usage):   Claude 5h/7d usage bars + animated mascot.
//   - Radar  (features/radar):   live ADS-B plane radar.
//   - HA / themes:               pushed MQTT screens and declarative clock faces.
// Shared plumbing (WiFi, web UI, OTA, display core, settings) lives at src root.
//
// License: WTFPL
#include <Arduino.h>
#include <stdio.h>
#include "Platform.h"
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Gfx.h"
#include "WebPortal.h"
#include "OtaUpdate.h"
#include "Mode.h"
#include "Clock.h"
#include "WgClient.h"
#if WITH_THEME
#include "features/theme/ThemeMode.h"
#endif
#if WITH_NOTIFY
#include "NotifyMode.h"
#endif

#if WITH_TICKER
#include "TickerMode.h"
#endif
#if WITH_USAGE
#include "UsageMode.h"
#endif
#if WITH_RADAR
#include "RadarMode.h"
#endif
#if WITH_HA
#include "HaMode.h"
#include "MqttClient.h"
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_TICKER
  &g_tickerMode,
#endif
#if WITH_USAGE
  &g_usageMode,
#endif
#if WITH_RADAR
  &g_radarMode,
#endif
#if WITH_HA
  &g_haMode,
#endif
#if WITH_THEME
  &g_themeMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// ---- carousel -------------------------------------------------------------
// MODE_CAROUSEL rotates through the ticked features. Switches call wake() on
// the incoming mode: repaint from cached data, no refetch.
static size_t   g_carIdx = 0;
static uint32_t g_carSwitch = 0;
static bool     g_carPaused = false;   // button paused the auto-rotation (stays put)

static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
#if WITH_TICKER
    case MODE_STOCKS: return s.carouselTicker;
#endif
#if WITH_USAGE
    case MODE_USAGE:  return s.carouselUsage;
#endif
#if WITH_RADAR
    case MODE_RADAR:  return s.carouselRadar;
#endif
#if WITH_HA
    case MODE_HA:     return s.carouselHa;
#endif
#if WITH_THEME
    case MODE_THEME:  return s.carouselTheme;
#endif
    default:          return true;
  }
}

// Advance g_carIdx to the next ticked mode (stays put if none other is ticked).
static void carouselNext(const Settings& s) {
  for (size_t hop = 1; hop <= kModeCount; hop++) {
    size_t cand = (g_carIdx + hop) % kModeCount;
    if (!carouselHas(s, kModes[cand])) continue;
    if (cand != g_carIdx) {
      g_carIdx = cand;
      kModes[cand]->wake(s);
    }
    return;
  }
}

static DisplayMode* activeMode(const Settings& s) {
  if (s.mode == MODE_CAROUSEL && kModeCount > 0) {
    if (g_carSwitch == 0) g_carSwitch = millis();
    if (!carouselHas(s, kModes[g_carIdx])) carouselNext(s);   // settings changed
    bool dwellExpired = millis() - g_carSwitch >= (uint32_t)s.carouselSec * 1000UL;
    if (g_carPaused) dwellExpired = false;   // button froze the rotation on the current page
    if (dwellExpired) {
      g_carSwitch = millis();
      carouselNext(s);
    }
    return kModes[g_carIdx];
  }
  for (size_t i = 0; i < kModeCount; i++)
    if (kModes[i]->modeConst() == s.mode) return kModes[i];
  return kModeCount ? kModes[0] : nullptr;   // fall back to the first compiled mode
}

#if WITH_THEME_PLAYLIST
// Playlist state stays small and transient; only the ordered IDs/durations
// live in Settings and survive reboot.
static uint8_t  g_themePlaylistIndex = 0;
static uint8_t  g_themePlaylistVisited = 0;
static uint32_t g_themePlaylistSince = 0;
static bool     g_themePlaylistRunning = false;
static bool     g_themePlaylistStopped = false;

static void themePlaylistReset() {
  g_themePlaylistIndex = 0;
  g_themePlaylistVisited = 0;
  g_themePlaylistSince = 0;
  g_themePlaylistRunning = false;
  g_themePlaylistStopped = false;
  g_themeMode.setPlaylistTheme(nullptr);
}

static bool themePlaylistSelectNext(const Settings& s, bool skipVisited) {
  const uint8_t count = s.themePlaylistCount;
  if (!count) return false;
  for (uint8_t hop = 1; hop <= count; ++hop) {
    uint8_t candidate = (g_themePlaylistIndex + hop) % count;
    uint8_t bit = uint8_t(1u << candidate);
    if (skipVisited && (g_themePlaylistVisited & bit)) continue;
    char path[64];
    snprintf(path, sizeof(path), "/themes/%s.stheme", s.themePlaylist[candidate].id);
    if (!LittleFS.exists(path)) {
      g_themePlaylistVisited |= bit;
      continue;
    }
    g_themePlaylistIndex = candidate;
    g_themePlaylistVisited |= bit;
    g_themePlaylistSince = 0;  // dwell starts only after the theme loads
    g_themePlaylistRunning = true;
    g_themeMode.setPlaylistTheme(s.themePlaylist[candidate].id);
    return true;
  }
  return false;
}

static void themePlaylistService(const Settings& s) {
  bool enabled = s.mode == MODE_THEME && s.themePlaylistEnabled && s.themePlaylistCount;
  if (!enabled) {
    if (g_themePlaylistRunning || g_themePlaylistStopped) themePlaylistReset();
    return;
  }
  if (g_themePlaylistStopped) return;
  if (!g_themePlaylistRunning) {
    // Start at the beginning on enable, after leaving another display mode,
    // or after a settings save.
    g_themePlaylistIndex = s.themePlaylistCount - 1;
    g_themePlaylistVisited = 0;
    if (!themePlaylistSelectNext(s, true)) {
      g_themeMode.setPlaylistTheme(nullptr);
      g_themePlaylistStopped = true;
    }
    return;
  }
  if (g_themeMode.playlistLoadFailed()) {
    // A corrupt package is skipped immediately. If every remaining entry is
    // missing or invalid, stop the playlist and fall back to the chosen theme.
    if (!themePlaylistSelectNext(s, true)) {
      g_themeMode.setPlaylistTheme(nullptr);
      g_themePlaylistRunning = false;
      g_themePlaylistStopped = true;
    }
    return;
  }
  if (!g_themeMode.playlistReady()) return;  // wait for memory or package load
  g_themePlaylistVisited = uint8_t(1u << g_themePlaylistIndex);
  if (!g_themePlaylistSince) g_themePlaylistSince = millis();
  uint32_t dwell = (uint32_t)s.themePlaylist[g_themePlaylistIndex].seconds * 1000UL;
  if (millis() - g_themePlaylistSince < dwell) return;
  if (!themePlaylistSelectNext(s, false)) {
    g_themeMode.setPlaylistTheme(nullptr);
    g_themePlaylistRunning = false;
    g_themePlaylistStopped = true;
  }
}
#endif

static Settings g_settings;
static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was a crash -> don't re-enter the crash path
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
static bool     g_fallbackTheme = false;
static bool     g_nativeRecovery = false;
static bool     g_recoveryAttemptMarked = false;
static uint8_t  g_crashStreak = 0;
static uint32_t g_bootStartedAt = 0;
#endif
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)
static uint32_t g_btnWakeUntil = 0;  // button wake overrides dimming until this millis()
#if HAS_LDR
static uint32_t g_lastAutoBr = 0;
static uint8_t  g_ldrCache   = DEFAULT_BRIGHTNESS;   // last LDR reading (2 s cadence)
#endif

#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
// RTC user memory survives exception/watchdog restarts without wearing flash.
// The first 128 bytes of the user area are reserved by eboot for OTA commands,
// so this record starts at word offset 32 (byte offset 128).
static constexpr uint32_t CrashRecordOffset = 32;
static constexpr uint32_t CrashRecordMagic = 0x53545643UL;  // "STVC"
static constexpr uint32_t CrashRecordSalt = 0x91C4A72BUL;
static constexpr uint8_t  CrashFallbackAfter = 3;
static constexpr uint32_t StableBootMs = 60000UL;
struct CrashRecord {
  uint32_t magic;
  uint32_t state;  // low byte: streak; bit 8: recovery theme already attempted
  uint32_t check;
};
static constexpr uint32_t CrashRecoveryThemeAttempted = 1UL << 8;
static constexpr uint32_t CrashStateMask = 0xFFUL | CrashRecoveryThemeAttempted;
static uint32_t crashRecordCheck(uint32_t state) {
  return CrashRecordMagic ^ state ^ CrashRecordSalt;
}
static void crashGuardBegin(bool crashed) {
  CrashRecord record{};
  uint32_t count = 0;
  uint32_t state = 0;
  bool valid = ESP.rtcUserMemoryRead(CrashRecordOffset,
      reinterpret_cast<uint32_t*>(&record), sizeof(record)) &&
      record.magic == CrashRecordMagic && !(record.state & ~CrashStateMask) &&
      (record.state & 0xFFUL) <= CrashFallbackAfter &&
      record.check == crashRecordCheck(record.state);
  if (valid) {
    state = record.state;
    count = state & 0xFFUL;
  }
  if (crashed && count < CrashFallbackAfter) ++count;
  state = (state & CrashRecoveryThemeAttempted) | count;
  g_crashStreak = static_cast<uint8_t>(count);
  g_fallbackTheme = count >= CrashFallbackAfter;
  g_nativeRecovery = g_fallbackTheme && (state & CrashRecoveryThemeAttempted);
  if (crashed) {
    record.magic = CrashRecordMagic;
    record.state = state;
    record.check = crashRecordCheck(state);
    ESP.rtcUserMemoryWrite(CrashRecordOffset,
        reinterpret_cast<uint32_t*>(&record), sizeof(record));
  }
}
static void crashGuardMarkRecoveryThemeAttempted() {
  if (g_recoveryAttemptMarked || g_nativeRecovery) return;
  CrashRecord record{};
  bool valid = ESP.rtcUserMemoryRead(CrashRecordOffset,
      reinterpret_cast<uint32_t*>(&record), sizeof(record)) &&
      record.magic == CrashRecordMagic && (record.state & 0xFFUL) == CrashFallbackAfter &&
      !(record.state & ~CrashStateMask) && record.check == crashRecordCheck(record.state);
  if (!valid) {
    record.magic = CrashRecordMagic;
    record.state = g_crashStreak;
  }
  record.state |= CrashRecoveryThemeAttempted;
  record.check = crashRecordCheck(record.state);
  ESP.rtcUserMemoryWrite(CrashRecordOffset,
      reinterpret_cast<uint32_t*>(&record), sizeof(record));
  g_recoveryAttemptMarked = true;
}
static void crashGuardService() {
  // A recovery boot stays out of the selected theme, but a clean recovery boot
  // can still age out the crash streak after a minute so the next reboot retries it.
  if ((g_safeMode && !g_fallbackTheme) || !g_crashStreak ||
      millis() - g_bootStartedAt < StableBootMs) return;
  CrashRecord record{CrashRecordMagic, 0, crashRecordCheck(0)};
  ESP.rtcUserMemoryWrite(CrashRecordOffset,
      reinterpret_cast<uint32_t*>(&record), sizeof(record));
  g_crashStreak = 0;
}
void appClearCrashStreak() {
  CrashRecord record{CrashRecordMagic, 0, crashRecordCheck(0)};
  ESP.rtcUserMemoryWrite(CrashRecordOffset,
      reinterpret_cast<uint32_t*>(&record), sizeof(record));
  g_crashStreak = 0;
  g_fallbackTheme = false;
  g_nativeRecovery = false;
  g_recoveryAttemptMarked = false;
}
#endif

// Single brightness resolver: night mode overrides auto-brightness overrides the
// manual level. Only writes the PWM when the effective target changes.
static uint8_t appEffectiveBrightness() {
  // A button wake press briefly lifts the screen to the manual level so a touch
  // reveals it even when night mode has dimmed or blanked the backlight.
  if (g_btnWakeUntil && millis() < g_btnWakeUntil) return g_settings.brightness;
  if (clockNightActive()) return g_settings.clock.nightLevel;
#if HAS_LDR
  if (g_settings.autoBrightness) {
    if (millis() - g_lastAutoBr > 2000) {
      g_lastAutoBr = millis();
      int raw = analogRead(LDR_PIN);
      g_ldrCache = (uint8_t)constrain(raw * 100 / ADC_MAX, 5, 100);
    }
    return g_ldrCache;
  }
#endif
  return g_settings.brightness;
}

void appApplyBrightness() {
  uint8_t t = appEffectiveBrightness();
  if ((int)t != g_lastBr) {
    g_lastBr = t;
    gfxSetBrightness(t, g_settings.backlightInverted);
  }
}

#ifdef BTN_PIN
// Capacitive touch button (e.g. NM-TV-154, GPIO32). 
// touchRead() returns a capacitance count that drops when a finger is present; the press
// threshold is calibrated from an idle baseline (recomputed if the configured
// sensitivity changes), since counts vary per unit. A short and a long press
// each run a configurable action (Settings.button). Non-blocking except for the
// deliberate ~1 s screen-off blink.
static uint32_t g_btnInfoUntil = 0;   // network-info screen shown until this millis()

static void buttonBlinkOff() {
  gfxSetBrightness(0, g_settings.backlightInverted);   // blank the panel
  delay(1000);
  g_lastBr = -1;                       // force the next tick to restore brightness
  appApplyBrightness();
}

static void buttonShowNetInfo() {
  gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
  g_btnInfoUntil = millis() + BTN_INFO_MS;
}

static void buttonWake() {
  g_btnWakeUntil = millis() + BTN_WAKE_MS;  // the brightness resolver lifts dimming for this window
  g_lastBr = -1;                            // force the next tick to apply it
  appApplyBrightness();
}

static void buttonAckNotify() {
#if WITH_NOTIFY
  g_notifyMode.dismiss();   // expire the active overlay now; the next one (or the feature) takes over
#endif
}

static void buttonGoHome() {
  g_carPaused = false;                 // resume normal rotation
  if (g_settings.mode == MODE_CAROUSEL && kModeCount > 0) {
    g_carIdx = 0;
    g_carSwitch = millis();
    if (!carouselHas(g_settings, kModes[0])) carouselNext(g_settings);  // 0 not ticked -> first ticked
    else kModes[0]->wake(g_settings);
  } else {
    DisplayMode* m = activeMode(g_settings);
    if (m) m->invalidate(g_settings);
  }
}

static void buttonNext() {
#if WITH_HA
  // Home Assistant mode rotates its own screens: step through them directly.
  if (g_settings.mode == MODE_HA) { g_haMode.nextPage(); return; }
#endif
  if (g_settings.mode != MODE_CAROUSEL || kModeCount == 0) return;
  g_carPaused = true;                  // manual navigation stops the auto-rotation
  g_carSwitch = millis();
  carouselNext(g_settings);            // advance + wake the incoming mode
}

static void buttonAction(uint8_t act) {
  switch (act) {
    case BTN_ACT_SCREEN_OFF: buttonBlinkOff();    break;
    case BTN_ACT_HOME:       buttonGoHome();      break;
    case BTN_ACT_NEXT:       buttonNext();        break;
    case BTN_ACT_NETINFO:    buttonShowNetInfo(); break;
    case BTN_ACT_WAKE:       buttonWake();        break;
    case BTN_ACT_ACK:        buttonAckNotify();   break;
    default: break;                                 // BTN_ACT_NONE
  }
}

static void buttonService() {
  static bool     inited   = false;
  static uint16_t thrDown  = 0;       // press  when median < thrDown
  static uint16_t thrUp    = 0;       // release when median > thrUp (hysteresis band)
  static uint8_t  sensSeen = 0;       // sensitivity the thresholds were computed for
  static bool     stable   = false;   // confirmed (debounced) button state
  static uint8_t  run      = 0;       // consecutive samples disagreeing with 'stable'
  static uint32_t downAt   = 0;

  // Calibrate (or recalibrate when the web UI changes the sensitivity).
  if (!inited || sensSeen != g_settings.button.sensitivity) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) { sum += touchRead(BTN_PIN); delay(2); }
    uint16_t baseline = sum / 16;
    uint8_t s = g_settings.button.sensitivity;
    thrDown = (uint16_t)((uint32_t)baseline * s / 100);
    uint8_t up = s + 5; if (up > 99) up = 99;            // ~5 % hysteresis band
    thrUp = (uint16_t)((uint32_t)baseline * up / 100);
    sensSeen = s;
    inited = true;
  }

  // ESP32 touchRead() emits isolated low-going glitch samples; a median of three
  // reads rejects any single spike, so a lone glitch can no longer be seen as a
  // press. Hysteresis then keeps the state from chattering near the threshold.
  uint16_t a = touchRead(BTN_PIN);
  uint16_t b = touchRead(BTN_PIN);
  uint16_t c = touchRead(BTN_PIN);
  uint16_t m = a > b ? (b > c ? b : (a > c ? c : a))
                     : (a > c ? a : (b > c ? c : b));

  bool wantDown = stable ? (m < thrUp) : (m < thrDown);
  if (wantDown == stable) { run = 0; return; }

  // Integrator debounce: require BTN_CONFIRM consecutive agreeing samples before
  // flipping the confirmed state, so a short burst of noise is ignored too.
  if (++run < BTN_CONFIRM) return;
  run = 0;
  stable = wantDown;
  if (stable) { downAt = millis(); return; }   // press start: decide on release
  uint32_t held = millis() - downAt;
  buttonAction(held >= BTN_LONG_MS ? g_settings.button.longAction
                                   : g_settings.button.shortAction);
}
#endif

// Exposed to the web portal (/api/status) so the last reset reason is visible.
const char* appResetReason() { return g_resetReason.c_str(); }

// Called by the web portal after settings are applied: re-init every mode and
// force a fresh repaint so a mode/URL/symbol change takes effect immediately.
void appInvalidate() {
#if WITH_THEME_PLAYLIST
  themePlaylistReset();
#endif
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->invalidate(g_settings);
}

// Called by the web portal before a request that needs a lot of heap, such as
// a settings save: every mode frees what it can rebuild on its next service().
void appRelease() {
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->freeForRequest();
}

static void bootProgress(const char* msg) {
  gfxBoot("SmallTV", msg);
}

void setup() {
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  g_bootStartedAt = millis();
#endif
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  // Capture why we (re)booted. On a reboot loop this is the key clue, and the
  // device's UART isn't exposed — so we also show it on screen below. On the
  // ESP8266 we also keep the crash PC (epc1) for addr2line decoding; the
  // ESP32-C2 (RISC-V) doesn't expose it, so epc/addr come back empty there.
  PlatformReset pr = platformResetInfo();
  Serial.print("[boot] reset reason: ");
  Serial.println(pr.reason);

  if (pr.wasCrash) {
    g_safeMode = true;                   // crashed last boot -> stay out of the crash path
    strlcpy(g_epcStr,  pr.epc,  sizeof(g_epcStr));
    strlcpy(g_addrStr, pr.addr, sizeof(g_addrStr));
    char rich[80];
    snprintf(rich, sizeof(rich), "%s epc %s addr %s", pr.reason.c_str(),
             g_epcStr[0] ? g_epcStr : "-", g_addrStr[0] ? g_addrStr : "-");
    g_resetReason = rich;
  } else {
    g_resetReason = pr.reason;
  }
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  crashGuardBegin(pr.wasCrash);
  if (g_fallbackTheme) g_safeMode = true;
#endif

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);

  Serial.println("[boot] display");
  gfxBegin(g_settings);
  gfxBoot(g_safeMode ? "Crashed" : "SmallTV", FW_VERSION);

  Serial.println("[boot] net");
  netAllowRetry(!g_safeMode);   // a retry's reboot would clear the crash flag
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that WiFi (STA) is up — but only if night mode is enabled, so a
  // ticker-only device doesn't pay the SNTP heap cost (which can starve the cash.ch
  // TLS handshake on the ESP8266). clockReapply arms it iff needed. Skipped after a
  // crash so a fault in here can't boot-loop before the web server starts (the
  // device then comes up in safe mode, OTA-recoverable, instead of needing UART).
  // ...unless a WireGuard tunnel is configured, which needs the clock and only
  // exists on an ESP32 where the heap argument for the skip does not apply.
  if (!g_safeMode || wgNeedsClock(g_settings)) clockReapply(g_settings);

  // Optional WireGuard tunnel (ESP32 targets). Arms the state machine only;
  // the bring-up itself runs from loop(), so nothing here can delay the web
  // server. A crash last boot feeds the three-strikes hold that keeps a bad
  // tunnel config from locking the device out of its own web UI.
  wgBegin(g_settings, g_safeMode);

  // A GitHub update queued from the web UI runs now, before the features claim
  // the heap (the download needs a 16 KB TLS buffer that only fits at boot).
  // On success it reboots into the new image; a no-op stub on the ESP32 targets.
  if (otaBootRequested()) {
    Serial.println("[boot] github update");
    gfxBoot("SmallTV", "updating...");
    otaBootUpdate(g_settings);
    gfxBoot("SmallTV", "update failed");   // still here -> failed; details in the web UI
    delay(1200);
  }

  Serial.println("[boot] web");
  webPortalBegin(g_settings);

#if WITH_HA
  mqttBegin(g_settings);   // arms the client; the connect itself runs from loop()
#endif

  Serial.println("[boot] modes");
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  if (g_fallbackTheme && !g_nativeRecovery) g_themeMode.useRecoveryTheme(g_crashStreak);
#endif
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str(), netFailReason());
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  } else if (g_fallbackTheme) {
    gfxSafeTheme(netIP().c_str(), g_crashStreak);
#endif
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
    delay(3500);
  }
}

void loop() {
  netLoop();
  webPortalLoop();
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
  crashGuardService();
#endif

#ifdef BTN_PIN
  buttonService();
  // The network-info screen (a long-press action) holds for BTN_INFO_MS, then
  // the current mode repaints over it. Handled here so it works in AP mode too.
  if (g_btnInfoUntil && millis() >= g_btnInfoUntil) {
    g_btnInfoUntil = 0;
    if (netMode() == NET_AP) {
      gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str(), netFailReason());
    } else {
      DisplayMode* m = activeMode(g_settings);
      if (m) m->wake(g_settings);
    }
  }
#endif

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  // Before the safe-mode return on purpose: if the crash had nothing to do with
  // the tunnel, remote access survives it, and if it did, the three-strikes hold
  // stops the retries by itself.
  wgService(g_settings);

  if (g_safeMode) {
#if defined(SMALLTV_CUSTOM) && defined(SMALLTV_FAMILY_ESP8266)
    if (netMode() == NET_AP) {
      static uint16_t shownFail = netFailSeq();
      if (netFailSeq() != shownFail) {
        shownFail = netFailSeq();
        gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str(), netFailReason());
      }
    } else if (g_fallbackTheme && !g_nativeRecovery) {
      crashGuardMarkRecoveryThemeAttempted();
      g_themeMode.service(g_settings);
    }
#endif
    delay(5);
    return;  // crashed last boot: web UI stays up for OTA recovery, no rendering
  }

#if WITH_HA
  // After the safe-mode return on purpose: a fault in here (e.g. a malformed
  // retained screen that re-arrives on every connect) must not boot-loop the
  // device past its own recovery page. Self-gates on WiFi STA + broker config.
  mqttLoop();
#endif

  if (netMode() == NET_AP) {
    // Setup mode: AP info stays on screen, redrawn when a background retry
    // of the saved networks fails with a new reason.
    static uint16_t shownFail = netFailSeq();
    if (netFailSeq() != shownFail) {
      shownFail = netFailSeq();
      gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str(), netFailReason());
    }
    delay(5);
    return;
  }

#ifdef BTN_PIN
  if (g_btnInfoUntil) {
    delay(5);
    return;  // network-info screen is up; don't let the mode repaint over it
  }
#endif

  // --- STA mode: the active feature fetches + renders itself ---

  // Night-mode state machine (NTP-trust gate), then apply the effective brightness
  // (night override / auto-brightness / manual level).
  clockService(g_settings);
  appApplyBrightness();

  // On expiry the carousel dwell is credited back the time it was hidden, so it
  // resumes on the same feature with the same remaining slice. heldMs() spans
  // the whole run rather than the last request in it, so a queue that chained
  // four overlays credits back all four.
#if WITH_NOTIFY
  static bool wasNotifying = false;
  if (g_notifyMode.active()) {
    wasNotifying = true;
    g_notifyMode.service(g_settings);
    delay(5);
    return;
  }
  bool restore = wasNotifying;
  if (wasNotifying) {
    wasNotifying = false;
    if (g_carSwitch) g_carSwitch += g_notifyMode.heldMs();
  }
#else
  const bool restore = false;
#endif

#if WITH_THEME_PLAYLIST
  themePlaylistService(g_settings);
#endif
  // A playlist may have selected/unloaded a theme above; display dispatch still
  // goes through the normal active-mode registry.
  DisplayMode* m = activeMode(g_settings);
  if (m) {
    if (restore) m->wake(g_settings);
    m->service(g_settings);
  }

  delay(5);
}
