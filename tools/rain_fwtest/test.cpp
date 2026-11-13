// test.cpp — validation du RainPng.cpp firmware sur PC
#include "stub_Arduino.h"
#include "stub_FS.h"
#include "RainPng.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s in.png out.raw cropK\n", argv[0]);
    return 2;
  }
  File src(argv[1], "rb");
  File dst(argv[2], "wb");
  if (!src || !dst) return 2;
  int cropK = atoi(argv[3]);
  uint8_t* win = new uint8_t[RAIN_WINDOW_PIXELS];
  memset(win, 0, RAIN_WINDOW_PIXELS);
  bool ok = rainDecodeToFs(src, dst, 256, cropK, win);
  delete[] win;
  printf("decode: %s, sortie=%u octets\n", ok ? "OK" : "ECHEC", dst.position());
  return ok ? 0 : 1;
}
