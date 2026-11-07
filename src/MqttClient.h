// MqttClient.h — MQTT connection for the Home Assistant screens feature.
//
// PubSubClient over a plain WiFiClient (non-TLS, LAN broker). Everything is
// driven from mqttLoop() off the main loop: connect only while WiFi STA is up,
// reconnect with a millis()-based backoff, never block waiting. The wire
// contract (docs: features/ha):
//   LWT  smalltv/<hostname>/availability = "offline", retained
//   on connect: publish "online" retained to the same topic, then subscribe
//   smalltv/<hostname>/screen/+ — one retained JSON screen per slot.
// Incoming screen messages go straight to the HaScreens store.
//
// Brightness over MQTT (kept in sync with the web UI):
//   command  smalltv/<hostname>/brightness/set  — plain integer 0..100;
//            applied through appApplyBrightness() like the web UI and persisted
//            debounced (~5 s) so a flapping slider doesn't wear the flash.
//   state    smalltv/<hostname>/brightness      — plain integer, retained;
//            published on every connect and whenever settings.brightness
//            changes from any source (poll-and-compare in mqttLoop).
//   discovery: a retained Home Assistant `number` config under
//            homeassistant/number/smalltv_<hostname>_brightness/config, plus a
//            homeassistant/status subscription so an HA restart ("online")
//            re-sends the config and the current state. Harmless on brokers
//            that aren't HA's.
//
// Broker settings changes (web UI save) are picked up live: the loop compares
// a snapshot of what it connected with against the current settings and
// reconnects when they differ.
#pragma once
#include <Arduino.h>
#include "Settings.h"

// The client and its receive buffer (MQTT_MAX_PACKET_SIZE bytes) are taken from
// the heap once, at boot, only when Home Assistant is wanted (HaBoot.h). Without
// them mqttLoop() and the publish helpers do nothing and report "not connected".
bool mqttAlloc();                    // build the client; false: no heap (nothing is kept)
void mqttFree();                     // drop it (harmless when there is none)

void mqttBegin(Settings& s);         // arm; connects from the loop, not here
void mqttLoop();                     // call every main-loop tick, after netLoop()

bool mqttConnected();                                        // broker link up right now
// Publish from outside the loop's own availability traffic. Returns false when
// not connected. An empty payload with retained=true is exactly how a broker
// deletes a retained message (used by /api/ha/clear to purge zombie screens).
bool mqttPublish(const char* topic, const char* payload, bool retained);
