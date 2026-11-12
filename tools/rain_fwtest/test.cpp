// test.cpp — validation du RainPng.cpp firmware sur PC
#include "stub_Arduino.h"
#include "stub_FS.h"
#include "RainPng.h"
#include <stdio.h>

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s in.png out.raw cropK\n", argv[0]);
    return 2;
  }
  File src(argv[1], "rb");
  File dst(argv[2], "wb");
  if (!src || !dst) return 2;
  int cropK = atoi(argv[3]);
  bool ok = rainDecodeToFs(src, dst, 256, cropK);
  printf("decode: %s, sortie=%u octets\n", ok ? "OK" : "ECHEC", dst.position());
  return ok ? 0 : 1;
}
