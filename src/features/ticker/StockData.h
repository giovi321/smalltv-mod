// StockData.h — runtime (volatile) data for one ticker
#pragma once
#include <Arduino.h>
#include "config.h"

struct StockData {
  char    symbol[MAX_SYMBOL_LEN];
  char    name[MAX_NAME_LEN];
  char    currency[6];
  char    rangeLabel[8];
  uint8_t source;     // SRC_* this ticker fetches from (copied from settings)
  float   qty;        // position size (copied from settings; 0 = no position)
  float   cost;       // cost basis per unit

  float price;
  float change;       // absolute change in price units
  float changePct;    // percentage change
  bool  hasChange;    // a change value was provided/derived

  float   spark[MAX_SPARK_POINTS];
  uint8_t sparkCount;

  bool     valid;     // has been successfully populated at least once
  bool     error;     // most recent fetch failed
  bool     lowHeap;   // ... because the TLS heap check skipped it (TlsGate.h)
  bool     userNamed; // user supplied a custom name (don't override from source)
  uint32_t lastOkMs;  // millis() of last good update

  // Per-symbol fetch schedule. Every ticker carries its own due time, so one
  // that fails (bad symbol, a provider hiccup, or an ESP8266 heap-guard skip)
  // is retried on its own short backoff instead of waiting out the shared poll
  // interval, and a healthy ticker is not re-fetched just because a neighbour
  // is failing.
  uint32_t nextTryMs; // millis() when this symbol is next due
  uint8_t  fails;     // consecutive failed attempts (drives the backoff)

  void clear() {
    symbol[0] = name[0] = currency[0] = rangeLabel[0] = 0;
    source = DEFAULT_SOURCE;
    qty = cost = 0;
    price = change = changePct = 0;
    hasChange = false;
    sparkCount = 0;
    valid = false;
    error = false;
    lowHeap = false;
    userNamed = false;
    lastOkMs = 0;
    nextTryMs = 0;
    fails = 0;
  }
};
