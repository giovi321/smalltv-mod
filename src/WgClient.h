// WgClient.h — optional WireGuard tunnel, so the device can be reached from
// outside the LAN without forwarding its plain-HTTP port to the internet.
//
// Compiled only where the chip has the flash and the RAM for it: the ESP32
// targets define SMALLTV_WIREGUARD in platformio.ini. On the ESP8266 (and any
// build without the flag) every function below is a no-op stub and the status
// reports compiledIn=false, so call sites need no #ifdef.
//
// The tunnel is driven from loop() as a small state machine (idle -> init ->
// connect -> allowed -> monitor) rather than its own task, and every call into
// the component is marshalled onto the lwIP thread, because esp_wireguard
// drives raw lwIP and Arduino's prebuilt IDF has core locking off.
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "StatusJson.h"

// Bring-up steps, reported in /api/status. Reading "enabled but still at
// connect" tells you the endpoint never answered, which the handshake
// timestamp alone cannot.
#define WG_STAGE_IDLE     0
#define WG_STAGE_INIT     1
#define WG_STAGE_CONNECT  2
#define WG_STAGE_ALLOWED  3
#define WG_STAGE_MONITOR  4

// Load the crash-hold counter and, if the config says so, arm the tunnel.
// bootWasCrash is main.cpp's own reset-reason verdict; three crash reboots in a
// row with the tunnel enabled hold it down so a bad config cannot lock the
// device out of its web UI.
void wgBegin(const Settings& s, bool bootWasCrash);

// Config changed: tear the tunnel down and re-arm it from the new settings.
// A deliberate re-save also lifts a crash hold — that is the operator saying
// the configuration is fixed, try again.
void wgReapply(const Settings& s);

// Drive the state machine. Cheap when the tunnel is off or already up.
void wgService(const Settings& s);

// True while the tunnel is suspended by the crash hold. The web portal uses it
// so that saving the WireGuard settings unchanged still lifts the hold.
bool wgHeld();

// Status for /api/status (always emitted, so the web UI can say "not in this
// build" rather than silently hiding the section).
smalltv::StatusWg wgStatus();

// True when a tunnel is configured and compiled in. WireGuard rejects
// handshakes stamped with a wrong clock, so this also decides whether NTP runs
// on a device that has night mode switched off.
bool wgNeedsClock(const Settings& s);
