// Arduino.h stub to build RainPng.cpp on the host
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PROGMEM
// Counted, so a test can check the decoder yields during long decodes.
inline unsigned long g_yieldCount = 0;
inline void yield() { g_yieldCount++; }
static inline uint8_t pgm_read_byte(const void* p) { return *(const uint8_t*)p; }
#define memcmp_P memcmp
#define PSTR(s) (s)
#define PGM_P const char*
#define strncpy_P strncpy
static inline uint16_t pgm_read_word(const uint16_t* p) { return *p; }
static inline uint32_t pgm_read_dword(const uint32_t* p) { return *p; }
