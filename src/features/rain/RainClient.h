// RainClient.h — fetches RainViewer radar frames (past 2h) for the rain radar.
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RainData.h"

void rainInit(const Settings& s);       // reset + poll ASAP
void rainService(const Settings& s);    // call often; self-times the polling
void rainForceRefresh();                // poll on the next service() call

uint8_t         rainCount();            // frames currently on disk (oldest first)
uint32_t        rainFrameTime(uint8_t i);
bool            rainFrameNowcast(uint8_t i);
uint32_t        rainLastOkMs();         // millis() of last good fetch (0 = never)
bool            rainError();            // most recent fetch failed
uint32_t        rainGenerated();        // unix time the server generated the set

// ---- diagnostics, reported by /api/status ---------------------------------
enum RainStage : uint8_t {
  RAIN_IDLE = 0,      // no poll has run yet
  RAIN_FETCHING,      // poll in progress (list step or frames being fetched)
  RAIN_NO_HOME,       // lat/lon still 0,0, so there is nothing to centre on
  RAIN_CONNECT_FAIL,  // http.begin() refused the URL, or the socket never opened
  RAIN_HTTP_ERROR,    // server answered with something other than 200
  RAIN_PARSE_FAIL,    // 200, but the JSON did not deserialize
  RAIN_NO_FRAMES,     // parsed fine, but no frames were listed
  RAIN_PNG_FAIL,      // a frame PNG failed to download or decode
  RAIN_FS_FAIL,       // LittleFS refused a file operation
  RAIN_OK,            // frames stored
};

RainStage  rainStage();                 // outcome of the most recent poll
const char* rainStageName();            // that outcome as a short string
int         rainLastHttp();             // last HTTP status, 0 = never got one
uint8_t     rainCropK();                // centre crop used by the last poll
uint32_t    rainLastTryMs();            // millis() of the last attempt
const String& rainLastUrl();            // URL of the last attempt
