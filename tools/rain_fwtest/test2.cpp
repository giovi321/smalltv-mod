// test2.cpp — two decodes in one process (shows any state kept between them).
// Both must succeed and give identical outputs: a static output state that
// begin() did not reset once made the second decode fail (pixel count
// 131072/196608).
// The LZ window is reused between the two decodes WITHOUT being cleared, as
// the firmware does between the frames of one fetch: a deflate
// back-reference only reads bytes written during the current decode, so a
// stale entry reaching the output would make the outputs differ.
#include "stub_Arduino.h"
#include "stub_FS.h"
#include "RainPng.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc < 4) return 2;
  char out2[1024];
  snprintf(out2, sizeof out2, "%s.2", argv[2]);
  uint8_t* win = new uint8_t[RAIN_WINDOW_PIXELS];
  memset(win, 0, RAIN_WINDOW_PIXELS);   // cleared once, at the start
  File src1(argv[1], "rb");
  File dst1(argv[2], "wb");
  int cropK = atoi(argv[3]);
  bool ok1 = rainDecodeToFs(src1, dst1, 256, cropK, win);
  printf("decode 1: %s\n", ok1 ? "OK" : "FAILED");
  src1.close(); dst1.close();
  File src2(argv[1], "rb");
  File dst2(out2, "wb");
  bool ok2 = rainDecodeToFs(src2, dst2, 256, cropK, win);   // same window, not cleared
  printf("decode 2: %s\n", ok2 ? "OK" : "FAILED");
  src2.close(); dst2.close();
  delete[] win;
  if (!ok1 || !ok2) return 1;
  // outputs identical byte for byte
  FILE* f1 = fopen(argv[2], "rb");
  FILE* f2 = fopen(out2, "rb");
  int same = 0;
  for (;;) {
    int c1 = fgetc(f1), c2 = fgetc(f2);
    if (c1 == EOF || c2 == EOF) { if (c1 != c2) same = 1; break; }
    if (c1 != c2) { same = 1; break; }
  }
  fclose(f1); fclose(f2);
  remove(out2);
  printf("identical outputs: %s\n", same ? "NO" : "YES");
  return same;
}
