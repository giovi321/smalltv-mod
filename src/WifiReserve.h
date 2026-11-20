// WifiReserve.h — heap the closed Wi-Fi SDK must still have on the ESP8266.
//
// The SDK allocates while it handles beacons and does not check the result:
// with too little heap left it writes through NULL (exc 29, memcpy at
// 0x4000df64; #132, #134). Measured on #132 (v3.9.0 release image, themes of
// growing size held 120 s with beacons and a theme data fetch running, three
// runs per step): every run that kept about 4 KB free held, below about 3.5 KB
// it crashed by chance. The largest block did not separate clean runs from
// crashes. https://github.com/giovi321/smalltv-mod/issues/132
#pragma once
#include <stdint.h>

// Lowest clean run (~4 KB) plus 1 KB of room.
constexpr uint32_t WifiReserveFreeHeap = 5120;
// Just under the lowest clean block; the same as ParseLargestBlock.
constexpr uint32_t WifiReserveLargestBlock = 1536;
