// HaBoot.h — Home Assistant memory, taken once at boot (features/ha).
//
// The screen store (HaScreens) and the MQTT client with its receive buffer
// (MqttClient) are big, and the ESP8266 has little heap. They used to be
// static, so every device carried them whether it used Home Assistant or not.
// Now haBoot() takes them from the heap at the start of setup(), before the
// display, the Wi-Fi and the web server fragment it, and only when Home
// Assistant is wanted. If the heap says no, Home Assistant stays off and the
// state says why; the device does not crash.
//
// "Wanted" is one rule, the one the web page has always shown: a broker host is
// set (see haWanted). There is no other switch. Changing the host between two
// hosts still takes effect at once (the client reconnects). Switching Home
// Assistant on or off, that is, filling or clearing the host, takes effect at
// the next boot, because that is when the memory is taken or given back.
#pragma once
#include "config.h"
#if WITH_HA
#include "Settings.h"

enum HaState : uint8_t {
  HA_STATE_OFF,         // not wanted at boot: nothing was taken
  HA_STATE_READY,       // store and client are in memory
  HA_STATE_NO_MEMORY,   // wanted, but the heap had no room; nothing is held
  HA_STATE_SAFE_MODE,   // wanted, but the last boot crashed: kept out, like the rest of the loop
};

// Home Assistant is wanted when a broker host is set.
inline bool haWanted(const Settings& s) { return s.ha.brokerHost.length() > 0; }

// Call once, at the start of setup() right after the settings are loaded.
// (Called again it starts over: it gives back what it holds first.)
HaState haBoot(bool wanted, bool safeMode);
HaState haState();

// What the web page should tell the user, or nullptr when there is nothing to
// say. `wantedNow` is haWanted() of the settings as they are now, which can
// differ from what the boot saw if the host was changed since.
//   "memory"  wanted, but the boot found no room (restart; leave the host blank to stop trying)
//   "safe"    wanted, but the device is in safe mode after a crash
//   "reboot"  the setting changed since boot: restart to switch it on or off
const char* haProblem(bool wantedNow);
#endif  // WITH_HA
