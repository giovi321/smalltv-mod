// stub Arduino.h + FS.h pour tester RainPng.cpp sur PC
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PROGMEM
static inline uint8_t pgm_read_byte(const uint8_t* p) { return *p; }
static inline uint16_t pgm_read_word(const uint16_t* p) { return *p; }
static inline uint32_t pgm_read_dword(const uint32_t* p) { return *p; }
