// UsageData.h — runtime (volatile) Claude usage snapshot from the daemon.
#pragma once
#include <Arduino.h>

struct UsageData {
  float    sessionPct;       // 5-hour window utilization (0..100)
  int      sessionResetMin;  // minutes until the 5-hour window resets
  float    weeklyPct;        // 7-day window utilization (0..100)
  int      weeklyResetMin;   // minutes until the 7-day window resets
  char     status[16];       // "normal"/"warning"/"rejected" from the usage
                             // endpoint; "allowed"/"allowed_warning"/"rejected"
                             // from older daemons. Both are accepted.

  bool     valid;            // populated at least once
  bool     error;            // most recent fetch failed
  uint32_t lastOkMs;         // millis() of last good update

  void clear() {
    sessionPct = weeklyPct = 0;
    sessionResetMin = weeklyResetMin = 0;
    status[0] = 0;
    valid = false;
    error = false;
    lastOkMs = 0;
  }
};
