// Stand-in for the core's <pgmspace.h>: on the host, flash is plain memory.
#pragma once
#include <stdint.h>
#define pgm_read_byte(p) (*(const uint8_t*)(p))
