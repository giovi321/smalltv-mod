// RainClient.h — fetches RainViewer radar frames (past 2h) for the rain radar.
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RainData.h"

void rainInit(const Settings& s);       // reset + poll ASAP
void rainService(const Settings& s);    // call often; self-times the polling
void rainForceRefresh();                // poll on the next service() call (ignored while a fetch is running)
void rainFreeForRequest();              // no-op: nothing held outside a fetch step (kept for the mode interface)
uint32_t rainFsFree();                  // LittleFS free bytes at the last room check (0 = none yet)
void rainRemoveAll();                   // rain turned off, or a theme upload starting: delete /rain
                                        // (frames, meta.json, temp files) and free what rain holds

uint8_t         rainCount();            // frames currently on disk (oldest first)
uint32_t        rainFrameTime(uint8_t i);
bool            rainFrameNowcast(uint8_t i);
bool            rainFramePath(uint8_t i, char* out, size_t n);   // LittleFS path of frame i
uint32_t        rainLastOkMs();         // millis() of last good fetch (0 = never)
bool            rainError();            // most recent fetch failed
uint32_t        rainGenerated();        // unix time the server generated the set
bool            rainFetching();         // a fetch is in progress
// The set is too old to show (see RAIN_STALE_SEC). nowUnix = 0 when the
// clock is not set.
bool            rainStale(uint32_t pollSec, uint32_t nowUnix);

// The wind arrow (rain.wind): the wind of the chosen level, as it comes
// (direction FROM, degrees from north) with its speed in km/h. False: the
// arrow is off, there is no usable answer yet, or the last one is older than
// the refresh period plus RAIN_STALE_SEC.
bool            rainWindNow(const Settings& s, int16_t& dirFrom, float& kmh);

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
  RAIN_NO_MEM,        // a fetch buffer could not be allocated: the download buffer, or both the
                      // 8 KB LZ window and the 2 KB small-memory ring (with the decoder state)
  RAIN_FS_FAIL,       // LittleFS refused a file operation
  RAIN_FS_FULL,       // not enough free space for another frame (see RAIN_FS_KEEP_FREE)
  RAIN_LOW_HEAP,      // a decode would take free heap under RAIN_DECODE_MIN_HEAP: tile skipped
  RAIN_SLOW_TILE,     // a decode ran past RAIN_DECODE_MAX_MS: tile skipped, and in later polls
  RAIN_OK,            // frames stored
};

RainStage  rainStage();                 // outcome of the most recent poll
// That outcome as a short string, copied into buf (n >= 32 holds every one;
// the names stay in flash). Returns buf.
const char* rainStageName(char* buf, size_t n);
int         rainLastHttp();             // last HTTP status, 0 = never got one
uint8_t     rainCropK();                // centre crop used by the last poll
uint32_t    rainLastTryMs();            // millis() of the last attempt
const char* rainLastUrl();              // URL of the last attempt ("" = none)
// Heap low-water mark of the current/last fetch (0 = no fetch yet) and the
// part of it that hit the lowest largest block ('W','L','D','X','S').
uint32_t    rainDecodeMs();            // duration of the last tile decode
bool        rainSmallMem();            // it ran in the small-memory mode (2 KB ring + LittleFS)
uint32_t    rainMinHeap();
// The wind request: millis() of the last good answer (0 = never), last HTTP
// status, whether one was made, and its outcome as a RainStage name.
uint32_t    rainWindOkMs();
int         rainWindHttp();
bool        rainWindTried();
const char* rainWindStageName(char* buf, size_t n);
uint32_t    rainMinBlk();
char        rainMinWhere();
