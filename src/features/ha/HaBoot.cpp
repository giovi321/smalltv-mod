// HaBoot.cpp — see HaBoot.h.
#include "HaBoot.h"
#if WITH_HA
#include "HaScreens.h"
#include "MqttClient.h"

static HaState g_state = HA_STATE_OFF;

HaState haBoot(bool wanted, bool safeMode) {
  haScreensFree();   // started over: nothing from an earlier call is kept
  mqttFree();
  if (!wanted) return g_state = HA_STATE_OFF;
  // Safe mode takes nothing: the web UI there must keep every byte for an
  // update, and the loop that would use Home Assistant does not run.
  if (safeMode) return g_state = HA_STATE_SAFE_MODE;
  // The store first: it is the big block (about 5.8 KB on the ESP8266).
  if (!haScreensAlloc()) return g_state = HA_STATE_NO_MEMORY;
  if (!mqttAlloc()) {
    haScreensFree();   // all or nothing: a store without a client is dead weight
    return g_state = HA_STATE_NO_MEMORY;
  }
  return g_state = HA_STATE_READY;
}

HaState haState() { return g_state; }

const char* haProblem(bool wantedNow) {
  if (wantedNow) {
    switch (g_state) {
      case HA_STATE_NO_MEMORY: return "memory";
      case HA_STATE_SAFE_MODE: return "safe";
      case HA_STATE_OFF:       return "reboot";   // switched on since boot
      case HA_STATE_READY:     return nullptr;
    }
  }
  // Switched off since boot: the memory is given back at the next boot. When
  // the boot found no room there is nothing held and nothing to restart for.
  return g_state == HA_STATE_READY ? "reboot" : nullptr;
}
#endif  // WITH_HA
