// Forced into the rain decoder's two files only (-include): on the host,
// flash reads are plain reads. Kept out of Arduino.h, which other host tests
// share with their own pgmspace.h.
#pragma once
#ifndef pgm_read_byte
#define pgm_read_byte(p) (*reinterpret_cast<const unsigned char*>(p))
#endif
#ifndef memcpy_P
#define memcpy_P memcpy
#endif
#ifndef strlen_P
#define strlen_P strlen
#endif
// Strings in flash on the device (PSTR, the _P functions): plain on the host.
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef memcmp_P
#define memcmp_P memcmp
#endif
#ifndef strcmp_P
#define strcmp_P strcmp
#endif
#ifndef strncmp_P
#define strncmp_P strncmp
#endif
#ifndef strncpy_P
#define strncpy_P strncpy
#endif
#ifndef snprintf_P
#define snprintf_P snprintf
#endif
// The decoder yields to the system during long decodes (RainPng.cpp); on the
// host there is nothing to yield to.
static inline void yield() {}
