#include "WgClient.h"
#include "Net.h"
#include "Platform.h"

#if defined(SMALLTV_WIREGUARD)

#include <LittleFS.h>
#include <esp_wireguard.h>
#include <esp_netif.h>
#include <lwip/ip4_addr.h>

// Consecutive crash reboots (with the tunnel enabled) before it is held down.
#define WG_CRASH_HOLD_AFTER  3
#define WG_CRASH_PATH        "/wgcrash"

#define WG_BACKOFF_MIN_MS    5000UL
#define WG_BACKOFF_MAX_MS   60000UL
#define WG_POLL_MS          10000UL      // peer-is-up poll while monitoring
#define WG_STALL_MS        180000UL      // no handshake this long -> rebuild
#define WG_CONNECT_TRIES        20       // 1 Hz DNS retries before giving this attempt up
#define WG_TIME_SANE    1577836800L      // 2020-01-01; below this the clock is unset
#define WG_TIME_WAIT_MS     90000UL      // grace for NTP before trying anyway

// The component keeps the const char* pointers from the config for the whole
// tunnel lifetime, so the strings must live in storage that is only rewritten
// while the tunnel is down. Hence fixed buffers rather than String::c_str().
static char s_priv[MAX_WG_KEY_LEN];
static char s_pub[MAX_WG_KEY_LEN];
static char s_host[MAX_WG_HOST_LEN];
static char s_addr[MAX_WG_ADDR_LEN];      // address only, no prefix
static char s_mask[16];
static String s_allowed;                  // parsed per bring-up, not held by the component

// Zero-initialised rather than the component's ESP_WIREGUARD_*_DEFAULT() macros:
// those expand to C designated initialisers, which are a GNU extension here.
static wireguard_config_t s_wgCfg;
static wireguard_ctx_t    s_ctx;

static uint8_t  s_stage = WG_STAGE_IDLE;
static bool     s_enabled = false;        // what the saved config asks for
static bool     s_armed = false;          // config says on and nothing holds us back
static bool     s_inited = false;         // esp_wireguard_init succeeded, needs teardown
static bool     s_up = false;
static bool     s_held = false;
static bool     s_badConfig = false;      // a field is missing or malformed
static bool     s_madeDefault = false;
static uint32_t s_attempts = 0;
static uint8_t  s_connectTries = 0;       // ESP_ERR_RETRY polls in the current attempt
static uint32_t s_backoffMs = WG_BACKOFF_MIN_MS;
static uint32_t s_nextStepMs = 0;
static uint32_t s_lastOkMs = 0;           // millis() of the last confirmed handshake
static uint32_t s_armedAtMs = 0;          // when we started waiting for the clock
static time_t   s_lastHandshake = 0;
static char     s_endpointIp[16] = "";

// ---- crash hold -----------------------------------------------------------
// One byte on LittleFS. Written only on a crash boot, on the first handshake,
// and on a config re-save — never per retry, on a device that retries every 5 s.
static uint8_t crashCountLoad() {
  File f = LittleFS.open(WG_CRASH_PATH, "r");
  if (!f) return 0;
  int v = f.read();
  f.close();
  return v > 0 ? (uint8_t)v : 0;
}

static void crashCountStore(uint8_t n) {
  if (n == 0) { LittleFS.remove(WG_CRASH_PATH); return; }
  File f = LittleFS.open(WG_CRASH_PATH, "w");
  if (!f) return;
  f.write(n);
  f.close();
}

// ---- lwIP thread marshalling ----------------------------------------------
// esp_wireguard drives raw lwIP (netif_add, dns_gethostbyname, udp_*), and the
// prebuilt Arduino IDF has CONFIG_LWIP_TCPIP_CORE_LOCKING off, so every call
// has to run on the tcpip thread instead of the Arduino loop task.
struct WgCall { esp_err_t rc; const char* a; const char* b; };

static esp_err_t doInit(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_init(&s_wgCfg, &s_ctx);
  return ESP_OK;
}
static esp_err_t doConnect(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_connect(&s_ctx);
  return ESP_OK;
}
static esp_err_t doDisconnect(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_disconnect(&s_ctx);
  return ESP_OK;
}
static esp_err_t doPeerUp(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_peer_is_up(&s_ctx);
  return ESP_OK;
}
static esp_err_t doAllow(void* v) {
  WgCall* c = (WgCall*)v;
  c->rc = esp_wireguard_add_allowed_ip(&s_ctx, c->a, c->b);
  return ESP_OK;
}
static esp_err_t doSetDefault(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_set_default(&s_ctx);
  return ESP_OK;
}
static esp_err_t doRestoreDefault(void* v) {
  ((WgCall*)v)->rc = esp_wireguard_restore_default(&s_ctx);
  return ESP_OK;
}
static esp_err_t doHandshake(void* v) {
  time_t t = 0;
  WgCall* c = (WgCall*)v;
  c->rc = esp_wireguard_latest_handshake(&s_ctx, &t);
  if (c->rc == ESP_OK) s_lastHandshake = t;
  return ESP_OK;
}

static esp_err_t wgExec(esp_netif_callback_fn fn, WgCall* c) {
  c->rc = ESP_FAIL;
  if (esp_netif_tcpip_exec(fn, c) != ESP_OK) return ESP_FAIL;
  return c->rc;
}

// ---- CIDR helpers ---------------------------------------------------------
static bool prefixToMask(int prefix, char* out, size_t n) {
  if (prefix < 0 || prefix > 32) return false;
  uint32_t m = prefix == 0 ? 0 : (0xFFFFFFFFu << (32 - prefix));
  snprintf(out, n, "%u.%u.%u.%u", (unsigned)((m >> 24) & 0xFF), (unsigned)((m >> 16) & 0xFF),
           (unsigned)((m >> 8) & 0xFF), (unsigned)(m & 0xFF));
  return true;
}

// Strict: String::toInt() answers 0 for "abc" and for an empty string, and a
// prefix of 0 means "route the whole internet through the tunnel". A typo must
// not be read as that, so anything but plain digits in range is rejected.
static bool parsePrefix(const String& s, int& out) {
  if (!s.length() || s.length() > 2) return false;
  for (unsigned i = 0; i < s.length(); i++)
    if (s[i] < '0' || s[i] > '9') return false;
  int v = s.toInt();
  if (v < 0 || v > 32) return false;
  out = v;
  return true;
}

static bool ipToU32(const String& ip, uint32_t& out) {
  ip4_addr_t a;
  if (!ip4addr_aton(ip.c_str(), &a)) return false;
  out = lwip_ntohl(a.addr);
  return true;
}

// Walk a comma/space separated CIDR list, handing each entry to fn as
// (network string, prefix). Returns how many entries were understood.
template <typename F>
static int forEachCidr(const String& list, F fn) {
  int n = 0, i = 0, len = (int)list.length();
  while (i < len) {
    while (i < len && (list[i] == ',' || list[i] == ' ')) i++;
    int j = i;
    while (j < len && list[j] != ',' && list[j] != ' ') j++;
    if (j > i) {
      String tok = list.substring(i, j);
      int slash = tok.indexOf('/');
      String net = slash < 0 ? tok : tok.substring(0, slash);
      int prefix = 32;
      bool ok = (slash < 0) || parsePrefix(tok.substring(slash + 1), prefix);
      if (ok && net.length()) { fn(net, prefix); n++; }   // a bad entry is skipped, not guessed
    }
    i = j;
  }
  return n;
}

// lwIP has no routing table, so the interface netmask alone decides which
// tunnel addresses are routable. A literal /32 matches nothing, so the netif
// takes the widest allowed-IPs range that still contains its own address
// (10.6.0.2/32 with allowed 10.6.0.0/24 comes up as 10.6.0.2/24). A /0 entry
// is skipped here: a full tunnel is handled by making WireGuard the default
// interface instead.
static int netifPrefixFor(const String& addr, const String& allowed, int fallback) {
  uint32_t ipv;
  if (!ipToU32(addr, ipv)) return fallback;
  int best = fallback;
  forEachCidr(allowed, [&](const String& net, int prefix) {
    if (prefix <= 0) return;                       // /0 -> default route, not a netmask
    uint32_t netv;
    if (!ipToU32(net, netv)) return;
    uint32_t mask = (prefix == 32) ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefix));
    if ((ipv & mask) != (netv & mask)) return;     // range does not contain our address
    if (prefix < best) best = prefix;              // widest containing range wins
  });
  return best;
}

// ---- tunnel lifecycle -----------------------------------------------------
static void tunnelTeardown() {
  // Keyed on the netif, not on s_inited: esp_wireguard_connect can fail after
  // esp_wireguard_init succeeded and leave ctx->netif NULL, and
  // esp_wireguard_disconnect dereferences it without checking.
  if (s_ctx.netif) {
    WgCall c;
    if (s_madeDefault) wgExec(doRestoreDefault, &c);
    wgExec(doDisconnect, &c);
    s_ctx.netif = nullptr;
  }
  s_inited = false;
  s_madeDefault = false;
  s_up = false;
  s_stage = WG_STAGE_IDLE;
}

static void backoff() {
  tunnelTeardown();
  s_nextStepMs = millis() + s_backoffMs;
  s_backoffMs = (s_backoffMs * 2 < WG_BACKOFF_MAX_MS) ? s_backoffMs * 2 : WG_BACKOFF_MAX_MS;
}

// Copy the settings into the buffers the component will hold pointers into,
// and fill wireguard_config_t. Only ever called with the tunnel down.
static bool stageConfig(const Settings& s) {
  strlcpy(s_priv, s.wg.privateKey.c_str(), sizeof(s_priv));
  strlcpy(s_pub,  s.wg.peerPublicKey.c_str(), sizeof(s_pub));
  strlcpy(s_host, s.wg.endpointHost.c_str(), sizeof(s_host));
  if (!s_priv[0] || !s_pub[0] || !s_host[0]) return false;

  String addr = s.wg.address;
  int slash = addr.indexOf('/');
  int prefix = 32;
  if (slash >= 0) {
    if (!parsePrefix(addr.substring(slash + 1), prefix)) return false;
    addr = addr.substring(0, slash);
  }
  if (!addr.length()) return false;
  if (prefix == 0) return false;   // a /0 tunnel address is a typo, not a request
  strlcpy(s_addr, addr.c_str(), sizeof(s_addr));
  // The component's netif_create rejects an address it cannot parse, and its
  // disconnect path is not safe to walk after that. Catch it here instead, so a
  // hostname, a typo, or a pasted "Address = ..." line stops the tunnel with a
  // message rather than failing halfway through bring-up.
  { ip4_addr_t probe; if (!ip4addr_aton(s_addr, &probe)) return false; }

  s_allowed = s.wg.allowedIps;
  if (!prefixToMask(netifPrefixFor(addr, s_allowed, prefix), s_mask, sizeof(s_mask)))
    return false;

  memset(&s_wgCfg, 0, sizeof(s_wgCfg));
  s_wgCfg.private_key          = s_priv;
  s_wgCfg.public_key           = s_pub;
  s_wgCfg.endpoint             = s_host;
  s_wgCfg.address              = s_addr;
  s_wgCfg.netmask              = s_mask;
  s_wgCfg.port                 = s.wg.endpointPort;
  s_wgCfg.persistent_keepalive = s.wg.keepalive;
  return true;
}

static void applyAllowedIps() {
  WgCall c;
  forEachCidr(s_allowed, [&](const String& net, int prefix) {
    if (prefix == 0) {                       // 0.0.0.0/0 -> route everything here
      if (wgExec(doSetDefault, &c) == ESP_OK) s_madeDefault = true;
      // Making the tunnel the default route only moves lwIP's routing decision.
      // The peer still needs a matching allowed source IP or every packet that
      // arrives at the interface is dropped again on the way out.
      c.a = "0.0.0.0";
      c.b = "0.0.0.0";
      wgExec(doAllow, &c);
      return;
    }
    char mask[16];
    if (!prefixToMask(prefix, mask, sizeof(mask))) return;
    c.a = net.c_str();
    c.b = mask;
    wgExec(doAllow, &c);
  });
}

// ---------------------------------------------------------------------------
void wgBegin(const Settings& s, bool bootWasCrash) {
  s_held = false;
  s_armed = false;
  s_badConfig = false;
  s_enabled = s.wg.enabled;
  if (!s.wg.enabled) {
    if (crashCountLoad()) crashCountStore(0);
    return;
  }

  uint8_t crashes = crashCountLoad();
  if (bootWasCrash) {
    if (crashes < 0xFF) crashes++;
    crashCountStore(crashes);
  } else if (crashes) {
    crashes = 0;                       // any clean boot forgets the streak
    crashCountStore(0);
  }
  if (crashes >= WG_CRASH_HOLD_AFTER) {
    // A tunnel bug must never brick the device out of its own web UI: hold it
    // down and let the user fix the config and save again.
    s_held = true;
    Serial.println("[wg] held down after repeated crash reboots");
    return;
  }

  s_armed = true;
  s_armedAtMs = millis();
  s_backoffMs = WG_BACKOFF_MIN_MS;
  s_nextStepMs = millis();
}

void wgReapply(const Settings& s) {
  tunnelTeardown();
  if (s_held || crashCountLoad()) {
    // A deliberate re-save is the operator saying "I fixed it, try again".
    s_held = false;
    crashCountStore(0);
  }
  s_attempts = 0;
  s_lastHandshake = 0;
  s_endpointIp[0] = 0;
  wgBegin(s, /*bootWasCrash=*/false);
}

void wgService(const Settings& s) {
  if (!s_armed || !s.wg.enabled) return;
  if (!netConnected()) return;                       // nothing to tunnel over yet
  if ((int32_t)(millis() - s_nextStepMs) < 0) return;

  WgCall c;
  switch (s_stage) {
    case WG_STAGE_IDLE: {
      // WireGuard stamps handshakes with the wall clock and the peer rejects a
      // wrong one as a replay. Wait for NTP, but not forever: a peer that has
      // never seen this device accepts whatever timestamp it first gets.
      if (time(nullptr) < WG_TIME_SANE && millis() - s_armedAtMs < WG_TIME_WAIT_MS) {
        s_nextStepMs = millis() + 2000;
        return;
      }
      if (!stageConfig(s)) {   // a field is missing or malformed: stop retrying
        s_armed = false;
        s_badConfig = true;
        Serial.println("[wg] configuration incomplete, tunnel not started");
        return;
      }
      s_attempts++;
      s_connectTries = 0;
      s_stage = WG_STAGE_INIT;
      return;
    }

    case WG_STAGE_INIT: {
      if (wgExec(doInit, &c) != ESP_OK) { backoff(); return; }
      s_inited = true;
      s_stage = WG_STAGE_CONNECT;
      return;
    }

    case WG_STAGE_CONNECT: {
      esp_err_t rc = wgExec(doConnect, &c);
      if (rc == ESP_ERR_RETRY) {
        // Endpoint DNS still in flight. A hostname that never resolves returns
        // this forever, so give it a bounded number of seconds and then fall
        // into the backoff rather than polling at 1 Hz for the rest of time.
        if (++s_connectTries < WG_CONNECT_TRIES) {
          s_nextStepMs = millis() + 1000;
          return;
        }
        Serial.println("[wg] endpoint did not resolve");
        backoff();
        return;
      }
      if (rc != ESP_OK) { backoff(); return; }
      if (s_ctx.config) {
        ip4_addr_t a = *ip_2_ip4(&s_ctx.config->endpoint_ip);
        strlcpy(s_endpointIp, ip4addr_ntoa(&a), sizeof(s_endpointIp));
      }
      s_stage = WG_STAGE_ALLOWED;
      return;
    }

    case WG_STAGE_ALLOWED: {
      applyAllowedIps();
      s_stage = WG_STAGE_MONITOR;
      s_backoffMs = WG_BACKOFF_MIN_MS;
      s_lastOkMs = millis();
      s_nextStepMs = millis() + WG_POLL_MS;
      return;
    }

    default: {                                        // WG_STAGE_MONITOR
      bool up = (wgExec(doPeerUp, &c) == ESP_OK);
      if (up && !s_up) {
        Serial.println("[wg] peer up");
        if (crashCountLoad()) crashCountStore(0);     // a handshake proves it is safe
      }
      s_up = up;
      if (up) {
        s_lastOkMs = millis();
        wgExec(doHandshake, &c);
      } else if (millis() - s_lastOkMs > WG_STALL_MS) {
        // Nothing for three minutes: rebuild from scratch so the endpoint
        // hostname is resolved again (dynamic-DNS peers move).
        Serial.println("[wg] stalled, rebuilding the tunnel");
        backoff();
        return;
      }
      s_nextStepMs = millis() + WG_POLL_MS;
      return;
    }
  }
}

smalltv::StatusWg wgStatus() {
  smalltv::StatusWg w;
  w.compiledIn = true;
  w.enabled = s_enabled;
  w.up = s_up;
  w.held = s_held;
  w.badConfig = s_badConfig;
  w.stage = s_stage;
  w.attempts = s_attempts;
  w.endpointIp = s_endpointIp;   // a static buffer, valid for the whole reply
  w.lastHandshake = (long)s_lastHandshake;
  return w;
}

bool wgHeld() { return s_held; }

bool wgNeedsClock(const Settings& s) { return s.wg.enabled; }

#else   // ---------------------------------------------------------------------
// Not compiled in (ESP8266, or any build without SMALLTV_WIREGUARD). The stubs
// keep every call site free of #ifdefs; the status says so explicitly so the
// web UI can explain the section is missing rather than hiding it.

void wgBegin(const Settings& s, bool bootWasCrash) { (void)s; (void)bootWasCrash; }
void wgReapply(const Settings& s) { (void)s; }
void wgService(const Settings& s) { (void)s; }

smalltv::StatusWg wgStatus() { return smalltv::StatusWg(); }   // compiledIn=false, the rest off

bool wgHeld() { return false; }

bool wgNeedsClock(const Settings& s) { (void)s; return false; }

#endif
