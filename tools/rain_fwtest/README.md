# rain_fwtest — validation du décodeur rain sur PC

Compile RainPng.cpp (code firmware) avec des stubs Arduino/FS minimaux et
décode une tuile RainViewer en `.raw` (240x240 indices kRainColors).

```bash
g++ -O2 -Dmemcpy_P=memcpy -I. -I../../src/features/rain test.cpp ../../src/features/rain/RainPng.cpp -o fwtest
./fwtest tuile.png sortie.raw 120            # sortie 57 600 octets

g++ -O2 -Dmemcpy_P=memcpy -I. -I../../src/features/rain test2.cpp ../../src/features/rain/RainPng.cpp -o fwtest2x
./fwtest2x tuile.png sortie.raw 120          # double décodage : doit afficher 65536 deux fois
```

`test2.cpp` vérifie le reset d'état de `QuantOut g_q` (bug 2x/3x vécu sur
l'Ultra : sans reset, le 2e décodage repart de pixels=65536).

Validation vs vérité terrain : `groundtruth.py` décode la tuile avec zlib,
prémultiplie l'alpha sur noir, croppe le centre KxK, upscale en bilinéaire
8.8 fixe 240x240 (réplique exacte de emitPair/emitRow) et quantifie via la
même LUT que le firmware (générée par `tools/make_rain_clut.py`, idempotence
incluse), puis compare au `.raw` via kRainColors — tolérance ±12/canal.

```bash
python3 groundtruth.py tuile.png sortie.raw 120
```

Référence (12/11/2026, 6 tuiles live) : 0,09–0,15 % au-delà de ±12/canal,
écart max 17–21 (les pixels restants sont les bords de nuages interpolés
entre couleurs quantifiées, ±1 pas de palette). Une tuile synthétique
constante (gris semi-transparent) donne 0 % exact.

Tuiles de test : `weather-maps.json` puis
`https://{host}{path}/256/7/47.151897/-1.608400/2/1_1.png` (le host du JSON
contient déjà `https://`).
