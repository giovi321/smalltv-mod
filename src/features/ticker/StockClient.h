// StockClient.h — fetches ticker data from the configured source
// (Yahoo Finance, cash.ch, or a custom webhook)
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "StockData.h"

void  stocksInit(const Settings& s);        // (re)build list from settings
void  stocksService(const Settings& s);     // call often; self-times the polling
void  stocksForceRefresh();                 // poll ASAP (e.g. after config save)

uint8_t          stocksCount();
const StockData& stockAt(uint8_t i);
bool             stocksAnyValid();
