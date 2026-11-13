// RadarClient.h — fetches nearby aircraft (adsb.lol / adsb.fi direct, or a LAN webhook).
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "RadarData.h"

void radarInit(const Settings& s);       // reset + poll ASAP
void radarService(const Settings& s);    // call often; self-times the polling
void radarForceRefresh();                // poll on the next service() call

uint8_t         radarCount();            // aircraft currently held (nearest first)
const Aircraft& aircraftAt(uint8_t i);
uint32_t        radarLastOkMs();         // millis() of last good fetch (0 = never)
bool            radarError();            // most recent fetch failed

// ---- diagnostics, reported by /api/status ---------------------------------
// Why a poll produced nothing. An empty scope on its own cannot tell you
// whether the device asked and was refused, or never asked at all, so the last
// outcome is recorded here rather than inferred from the screen.
enum RadarStage : uint8_t {
  RADAR_IDLE = 0,      // no poll has run yet
  RADAR_NO_HOME,       // lat/lon still 0,0, so there is nothing to centre on
  RADAR_LOW_HEAP,      // skipped: largest free block under the TLS floor
  RADAR_CONNECT_FAIL,  // http.begin() refused the URL, or the socket never opened
  RADAR_HTTP_ERROR,    // server answered with something other than 200
  RADAR_PARSE_FAIL,    // 200, but the JSON did not deserialize (truncated stream?)
  RADAR_NO_AC,         // parsed fine, but the "ac" array was missing or empty
  RADAR_FILTERED_ALL,  // parsed fine, aircraft present, all dropped by minAltFt
  RADAR_OK,            // aircraft plotted
};

RadarStage  radarStage();                // outcome of the most recent poll
const char* radarStageName();            // that outcome as a short string
int         radarLastHttp();             // last HTTP status, 0 = never got one
uint16_t    radarTlsRx();                // negotiated BearSSL rx buffer (0 = not probed)
uint16_t    radarSeenAc();               // aircraft in the last parsed response
uint32_t    radarLastTryMs();            // millis() of the last attempt
const String& radarLastUrl();            // URL of the last attempt (which feed, what range)

const char* radarParseError();           // JSON error, empty when no parse failed
int         radarTlsError();             // BearSSL error, 0 when none recorded
