// test2.cpp — double décodage dans un même processus (révèle l'état statique).
// Les deux passages doivent réussir et produire des sorties identiques : le
// bug 2x/3x vécu (QuantOut g_q statique non remis à zéro dans begin()) faisait
// échouer le 2e décodage (compte de pixels 131072/196608).
// La fenêtre LZ est réutilisée entre les deux décodages SANS être nettoyée,
// comme le fait le firmware entre les frames d'un même fetch : une
// back-référence deflate ne lit que des octets émis pendant le décodage
// courant, donc des entrées périmées nuiraient → sorties différentes.
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
  memset(win, 0, RAIN_WINDOW_PIXELS);   // nettoyée une seule fois, au départ
  File src1(argv[1], "rb");
  File dst1(argv[2], "wb");
  int cropK = atoi(argv[3]);
  bool ok1 = rainDecodeToFs(src1, dst1, 256, cropK, win);
  printf("decode 1: %s\n", ok1 ? "OK" : "ECHEC");
  src1.close(); dst1.close();
  File src2(argv[1], "rb");
  File dst2(out2, "wb");
  bool ok2 = rainDecodeToFs(src2, dst2, 256, cropK, win);   // même fenêtre, non nettoyée
  printf("decode 2: %s\n", ok2 ? "OK" : "ECHEC");
  src2.close(); dst2.close();
  delete[] win;
  if (!ok1 || !ok2) return 1;
  // sorties octet à octet identiques
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
  printf("sorties identiques: %s\n", same ? "NON" : "OUI");
  return same;
}
