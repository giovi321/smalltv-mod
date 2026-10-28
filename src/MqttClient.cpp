// MqttClient.cpp — see MqttClient.h for the contract and the wire topics.
#include "MqttClient.h"
#include "config.h"
#if WITH_HA
#include <PubSubClient.h>
#include "Platform.h"   // brings in WiFi.h / ESP8266WiFi.h per target
#include "Net.h"
#include "HaScreens.h"
#include "DebouncedSave.h"

// Defined in main.cpp — re-resolve effective brightness now (the same call the
// web UI's /api/config handler makes after a save).
extern void appApplyBrightness();
// Defined in main.cpp — every mode frees what it can rebuild (a running theme
// on the ESP8266), as before the web UI's settings save.
extern void appRelease();

// Reconnect backoff: 5 s after a failure, doubling up to a 60 s cap; reset on
// a successful connect.
#define MQTT_RETRY_MIN_MS  5000UL
#define MQTT_RETRY_MAX_MS 60000UL

// Delay between the last brightness/set command and the saveSettings() write:
// a dragged slider can flap, so the flash write waits for quiet.
#define MQTT_SAVE_DEBOUNCE_MS 5000UL
// A write that failed (out of memory) is tried again this much later. Each
// try can free the running theme, so not more often than this.
#define MQTT_SAVE_RETRY_MS 30000UL

static Settings*    S = nullptr;
static WiFiClient   g_tcp;
static PubSubClient g_mqtt(g_tcp);

static uint32_t g_backoffMs   = MQTT_RETRY_MIN_MS;
static uint32_t g_nextAttempt = 0;

// Snapshot of what the current connection (or attempt) was made with; a save
// from the web UI that changes any of it forces a reconnect.
static char     g_connHost[MAX_HA_HOST_LEN] = "";
static uint16_t g_connPort = 0;
static char     g_connUser[MAX_HA_USER_LEN] = "";
static char     g_connPass[MAX_HA_PASS_LEN] = "";

// Topics built once from the hostname at mqttBegin(): a hostname change comes
// with a WiFi change, which reboots, so they can't go stale within a boot.
static char g_availTopic[96];    // smalltv/<hostname>/availability (also the LWT)
static char g_screenSub[104];    // smalltv/<hostname>/screen/+
static char g_stateTopic[104];   // smalltv/<hostname>/brightness
static char g_setTopic[112];     // smalltv/<hostname>/brightness/set
static char g_discTopic[128];    // homeassistant/number/smalltv_<hostname>_brightness/config

static int      g_pubBrightness = -1;   // last value sent to the state topic
static bool     g_haOnline = false;     // HA announced a restart: re-announce
static DebouncedSave g_save(MQTT_SAVE_DEBOUNCE_MS, MQTT_SAVE_RETRY_MS);   // brightness/set

static bool brokerChanged() {
  return g_connPort != S->ha.brokerPort
      || strncmp(g_connHost, S->ha.brokerHost.c_str(), MAX_HA_HOST_LEN)
      || strncmp(g_connUser, S->ha.brokerUser.c_str(), MAX_HA_USER_LEN)
      || strncmp(g_connPass, S->ha.brokerPass.c_str(), MAX_HA_PASS_LEN);
}

// Retained plain-integer state publish. g_pubBrightness is only updated on a
// successful publish, so a failed one is retried by the loop's compare.
static void publishState() {
  char buf[4];
  snprintf(buf, sizeof(buf), "%u", (unsigned)S->brightness);
  if (g_mqtt.publish(g_stateTopic, buf, /*retained=*/true))
    g_pubBrightness = (int)S->brightness;
}

// Home Assistant MQTT discovery: one `number` entity bound to the set/state
// topics, availability via the existing LWT topic. A pathological hostname
// that would truncate the JSON simply skips the publish (state/command topics
// still work; only auto-discovery is lost).
static void publishDiscovery() {
  char payload[512];
  int n = snprintf(payload, sizeof(payload),
    "{\"name\":\"Brightness\",\"uniq_id\":\"smalltv_%s_brightness\","
    "\"cmd_t\":\"%s\",\"stat_t\":\"%s\","
    "\"min\":0,\"max\":100,\"step\":1,\"mode\":\"auto\","
    "\"avty_t\":\"%s\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\","
    "\"dev\":{\"ids\":\"smalltv_%s\",\"name\":\"SmallTV %s\",\"mf\":\"SmallTV\"}}",
    S->hostname.c_str(), g_setTopic, g_stateTopic, g_availTopic,
    S->hostname.c_str(), S->hostname.c_str());
  if (n < 0 || (size_t)n >= sizeof(payload)) return;
  g_mqtt.publish(g_discTopic, payload, /*retained=*/true);
}

// Brightness command: a plain integer 0..100 (NOT JSON). Non-numeric payloads
// and trailing garbage are ignored; out-of-range values are clamped. Applied
// through the same resolver the web UI uses and persisted on the debounce
// timer, so a flapping slider costs at most one flash write per quiet spell.
static void onBrightnessSet(const uint8_t* payload, unsigned int len) {
  char buf[8];
  if (!len || len >= sizeof(buf)) return;
  memcpy(buf, payload, len);
  buf[len] = 0;
  char* end;
  long v = strtol(buf, &end, 10);
  if (end == buf) return;                       // not a number at all
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
  if (*end) return;                             // trailing garbage
  uint8_t val = (uint8_t)constrain((int)v, 0, 100);
  if (S->brightness == val) return;             // no change, nothing to do
  S->brightness = val;
  appApplyBrightness();                         // respects night/auto/manual
  g_save.request(millis());                     // persist once the flap stops
  // The state publish happens from mqttLoop()'s poll-and-compare, same as a
  // web UI change — one code path covers both sources.
}

static void onMessage(char* topic, uint8_t* payload, unsigned int len) {
  if (!strcmp(topic, g_setTopic)) { onBrightnessSet(payload, len); return; }
  // HA announces its restart as "online" on homeassistant/status: the config
  // and state re-send runs from the loop, not from inside this callback.
  if (!strcmp(topic, "homeassistant/status")) {
    if (len == 6 && !memcmp(payload, "online", 6)) g_haOnline = true;
    return;
  }
  // smalltv/<hostname>/screen/<slot> — the slot is everything after the last
  // '/', used as-is (no case folding; the docs' examples are all lowercase).
  const char* slash = strrchr(topic, '/');
  if (!slash || !slash[1]) return;
  haScreensApply(slash + 1, payload, len);
}

void mqttBegin(Settings& s) {
  S = &s;
  g_mqtt.setCallback(onMessage);
  snprintf(g_availTopic, sizeof(g_availTopic), "smalltv/%s/availability", S->hostname.c_str());
  snprintf(g_screenSub, sizeof(g_screenSub), "smalltv/%s/screen/+", S->hostname.c_str());
  snprintf(g_stateTopic, sizeof(g_stateTopic), "smalltv/%s/brightness", S->hostname.c_str());
  snprintf(g_setTopic, sizeof(g_setTopic), "smalltv/%s/brightness/set", S->hostname.c_str());
  snprintf(g_discTopic, sizeof(g_discTopic),
           "homeassistant/number/smalltv_%s_brightness/config", S->hostname.c_str());
}

bool mqttConnected() { return g_mqtt.connected(); }

bool mqttPublish(const char* topic, const char* payload, bool retained) {
  if (!g_mqtt.connected()) return false;
  return g_mqtt.publish(topic, payload, retained);
}

void mqttLoop() {
  haScreensService();   // debounced persist, even with no broker configured

  // Debounced brightness persist, also with no broker configured: a command
  // that arrived just before the broker was cleared still lands. The other
  // save paths (web UI) write immediately and don't touch this timer — a
  // later debounced write then simply rewrites the same current settings.
  if (!S) return;
  g_save.service(millis(), [] {
    appRelease();   // building the file's document needs heap a theme holds
    return saveSettings(*S);
  });

  // No broker configured: the feature is off. Dropping an existing connection
  // here is what turns a cleared broker host into a clean disconnect.
  if (!S->ha.brokerHost.length()) {
    if (g_mqtt.connected()) g_mqtt.disconnect();
    g_connHost[0] = 0;    // counts as "changed" when a host is set again
    return;
  }

  // Only connect while the station is up (never in AP/setup mode).
  if (netMode() != NET_STA || !netConnected()) {
    if (g_mqtt.connected()) g_mqtt.disconnect();
    return;
  }

  if (g_mqtt.connected()) {
    if (brokerChanged()) g_mqtt.disconnect();   // reconnect below, next tick
    else {
      g_mqtt.loop();
      // An HA restart ("online" on homeassistant/status) re-sends the
      // discovery config and forces a state republish.
      if (g_haOnline) {
        g_haOnline = false;
        publishDiscovery();
        g_pubBrightness = -1;
      }
      // Poll-and-compare: any settings.brightness change (web UI, MQTT
      // command, config import) is announced exactly once; an unchanged value
      // is never republished.
      if ((int)S->brightness != g_pubBrightness) publishState();
      return;
    }
  }

  // Disconnected. A settings change retries immediately; failures back off.
  uint32_t now = millis();
  if (brokerChanged()) g_nextAttempt = now;
  if ((int32_t)(now - g_nextAttempt) < 0) return;
  g_nextAttempt = now + g_backoffMs;
  // Double the backoff up to the cap (written out: ESP8266's min() template
  // rejects the mixed uint32_t/unsigned-long types).
  if (g_backoffMs < MQTT_RETRY_MAX_MS / 2) g_backoffMs *= 2;
  else g_backoffMs = MQTT_RETRY_MAX_MS;

  strlcpy(g_connHost, S->ha.brokerHost.c_str(), sizeof(g_connHost));
  g_connPort = S->ha.brokerPort;
  strlcpy(g_connUser, S->ha.brokerUser.c_str(), sizeof(g_connUser));
  strlcpy(g_connPass, S->ha.brokerPass.c_str(), sizeof(g_connPass));

  g_mqtt.setServer(g_connHost, g_connPort);
  // PubSubClient::connect() registers the LWT (retained "offline") and does a
  // synchronous TCP connect — a dead broker IP can stall the loop for the
  // TCP timeout here, once per backoff interval.
  if (!g_mqtt.connect(S->hostname.c_str(),
                      g_connUser[0] ? g_connUser : nullptr,
                      g_connPass[0] ? g_connPass : nullptr,
                      g_availTopic, 0, true, "offline")) {
    return;
  }

  g_backoffMs = MQTT_RETRY_MIN_MS;
  // Both availability messages are retained, so a `mosquitto_sub -C 1` always
  // gets the current state immediately (docs verify it that way).
  g_mqtt.publish(g_availTopic, "online", /*retained=*/true);
  // Brightness entity: discovery config + current state, both retained.
  publishDiscovery();
  publishState();
  g_mqtt.subscribe(g_screenSub);
  g_mqtt.subscribe(g_setTopic);
  g_mqtt.subscribe("homeassistant/status");
  g_mqtt.loop();
}

#endif  // WITH_HA
