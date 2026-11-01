// Clock.h — SNTP wall-clock + scheduled night-mode window with an NTP-trust gate.
//
// Owns the device's only notion of real time: arms SNTP with the configured
// POSIX TZ rule (DST handled by the rule), and runs the night-mode state machine.
// Night mode only switches ON when the clock was confirmed by a recent NTP sync
// (clockTrusted): if NTP can't be reached the screen stays on and SNTP is re-armed
// until a fresh sync lands or the window ends. Once on, it stays on until the
// window ends (morning). clockService() drives this; clockNightActive() reads it.
#pragma once
#include "Settings.h"
#include <time.h>

void   clockBegin(const Settings& s);        // arm SNTP + register sync callback (once WiFi is up)
void   clockReapply(const Settings& s);      // re-arm iff tzPosix changed
void   clockForceResync(const Settings& s);  // re-arm SNTP now (fresh query)
void   clockService(const Settings& s);      // drive the night-mode state machine (call each loop)

bool   clockSynced();                        // SNTP has ever set the clock
bool   clockTrusted();                       // synced within the trust window (clock believed correct now)
bool   clockNow(struct tm& out);             // local broken-down time; false if unsynced

// Sun schedule (SunTimes.h): the backlight steps for right now, between the
// lowest level and highSteps. False when it cannot run (off, no location,
// clock not set yet): the caller then uses the normal brightness.
bool   clockSunSteps(const Settings& s, uint8_t highSteps, uint8_t& steps);
bool   clockSunText(const Settings& s, char* buf, size_t n);   // "07:12-17:03", "polar day", "polar night"

bool   clockNightActive();                   // night mode is on right now (from clockService)
bool   clockNightHeld();                     // in the window but held off waiting for a fresh NTP sync

String clockTimeStr();                        // "YYYY-MM-DD HH:MM" or "" if unsynced
