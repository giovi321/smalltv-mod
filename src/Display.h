// Display.h — ST7789 rendering for the SmallTV
#pragma once
#include <Arduino.h>
#include "Settings.h"
#include "StockData.h"
#include "UsageData.h"

void displayBegin(const Settings& s);
void displaySetBrightness(uint8_t pct, bool inverted);
void displaySetRotation(uint8_t r);

// Status / boot screens
void displayBootMessage(const char* line1, const char* line2);
void displayApInfo(const char* ssid, const char* pass, const char* ip);
void displayStaInfo(const char* ssid, const char* ip, const char* host);
void displayMessage(const char* title, const char* msg, uint16_t titleColor);

// Main ticker view
void displayStock(const StockData& d, uint8_t pageIndex, uint8_t pageCount,
                  const Settings& s);

// Claude usage view
void displayUsage(const UsageData& u, const Settings& s);   // stats: mascot + 5h/7d meters
void displayMascot(const uint8_t* cells, const uint16_t* palette, bool restart); // idle anim
