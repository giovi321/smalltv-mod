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
// Last poll outcome, including failures while older aircraft remain cached.
// An empty display alone cannot distinguish a skipped request, a network or
// parser failure, an empty feed, and entries without usable display data.
enum RadarStage : uint8_t {
  RADAR_IDLE = 0,      // no poll has run yet
  RADAR_NO_HOME,       // lat/lon still 0,0, so there is nothing to centre on
  RADAR_LOW_HEAP,      // skipped: largest free block under the TLS floor
  RADAR_CONNECT_FAIL,  // begin() failed or a transport error before the response
  RADAR_HTTP_ERROR,    // server answered with something other than 200
  RADAR_PARSE_FAIL,    // HTTP 200, but JSON framing/parsing/allocation failed
  RADAR_NO_AC,         // no supported ac/aircraft array, or selected array empty
  RADAR_FILTERED_ALL,  // entries present, none with usable coordinates/altitude
  RADAR_NO_MFLN,       // ESP8266: direct feed lacks MFLN; no poll is sent
  RADAR_OK,            // aircraft retained; range/rim settings govern rendering
  RADAR_ENCODING_ERROR,// HTTPClient rejected an unsupported transfer encoding
  RADAR_TIMEOUT,       // overall HTTP header/body budget exhausted
};

RadarStage  radarStage();                // outcome of the most recent poll
const char* radarStageName();            // that outcome as a short string
int         radarLastHttp();             // last GET result: >0 HTTP, <0 client error, 0 none
uint16_t    radarTlsRx();                // chosen ESP8266 TLS buffer; 0 before probe/on ESP32
uint16_t    radarSeenAc();               // accepted array count, capped at 65535; reset on fetch
uint32_t    radarLastTryMs();            // millis() of the last attempt
const String& radarLastUrl();            // URL of the last attempt (which feed, what range)

const char* radarParseError();           // JSON error, empty when no parse failed
