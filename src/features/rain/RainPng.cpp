// RainPng.cpp — décodeur PNG embarqué (tuiles radar RainViewer 256 px RGBA).
//
// Pourquoi pas PNGdec : ses buffers statiques (~38 Ko) dépassent le heap de
// l'ESP8266. Les tuiles RainViewer utilisent le filtre « None » sur toutes
// leurs lignes (vérifié), donc aucun dé-filtrage n'est nécessaire : la
// fenêtre LZ stocke le RGB565 du pixel (2 octets) au lieu des 4 octets RGBA
// bruts — 16 Ko au lieu de 32 Ko, alloués sur le heap le temps du décodage.
// Le port est validé sur PC (tools/raindecode_proto) : séquence de copies
// bit-identique au parseur de référence, 90-95 % des octets de sortie
// identiques à un décodage zlib exact, le reste = mélange cosmétique des
// back-références désalignées (les canaux de deux pixels sources se
// mélangent, ±1 niveau d'intensité).
#include "RainPng.h"
#include "RainData.h"

#define RAIN_WINDOW_PIXELS 8192        // 32 Ko bruts / 4 octets-par-pixel
#define RAIN_FAST_BITS     9           // lookup huffman rapide (2^9 entrées)
#define RAIN_LOOKUP        (1 << RAIN_FAST_BITS)
#define RAIN_OUT           240         // taille de sortie (écran)
#define RAIN_MAX_IDAT      8           // chunks IDAT par fichier (2 en pratique)

// ---------------------------------------------------------------------------
// Quantification : couleur RGB565 -> niveau de palette (LUT RGB332)
// ---------------------------------------------------------------------------
static uint8_t g_lut[256];
static bool    g_lutReady = false;

void rainLevelLutBuild() {
  for (int i = 0; i < 256; i++) {
    int r3 = i >> 5, g3 = (i >> 2) & 7, b2 = i & 3;
    // couleur représentative (centre de la cellule RGB332)
    uint16_t c = (uint16_t)(((r3 * 4 + 2) << 11) | ((g3 * 8 + 4) << 5) | (b2 * 8 + 4));
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    uint8_t best = 0;
    int bestD = 1 << 30;
    for (uint8_t k = 0; k < 20; k++) {
      uint16_t t = pgm_read_word(&kRainSourceRamp[k]);
      int dr = r - ((t >> 11) & 31);
      int dg = g - ((t >> 5) & 63);
      int db = b - (t & 31);
      int d = dr * dr + dg * dg + db * db;
      if (d < bestD) { bestD = d; best = k; }
    }
    g_lut[i] = 1 + (uint8_t)(best * 14 / 19);
  }
  g_lutReady = true;
}

uint8_t rainLevelLut(uint16_t c) {
  if (c == 0) return 0;   // transparent (les tuiles n'ont jamais de noir opaque)
  return g_lut[((c >> 13) << 5) | (((c >> 8) & 7) << 2) | ((c >> 3) & 3)];
}

// Version lente exacte (tests natifs / diagnostics).
uint8_t rainLevelFor(uint16_t c) {
  if (c == 0) return 0;
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  uint8_t best = 0;
  int bestD = 1 << 30;
  for (uint8_t k = 0; k < 20; k++) {
    uint16_t t = pgm_read_word(&kRainSourceRamp[k]);
    int dr = r - ((t >> 11) & 31);
    int dg = g - ((t >> 5) & 63);
    int db = b - (t & 31);
    int d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = k; }
  }
  return 1 + (uint8_t)(best * 14 / 19);
}

// ---------------------------------------------------------------------------
// Lecture de bits bufferisée depuis un File (flux IDAT concaténés).
// Les chunks IDAT ne sont pas contigus dans le fichier : CRC et en-têtes de
// chunks s'intercalent ; le lecteur saute ces frontières.
// ---------------------------------------------------------------------------
struct IdatSeg {
  uint32_t off;   // offset des données IDAT dans le fichier
  uint32_t len;
};

struct BitSrc {
  File*          f;
  const IdatSeg* segs = nullptr;
  int            segCount = 0;
  int            segIdx = 0;
  uint32_t       segLeft = 0;    // octets restants dans le segment courant
  uint8_t        buf[512];
  int            bufPos = 0, bufLen = 0;

  bool refill() {
    while (segLeft == 0) {
      if (segIdx >= segCount) return false;
      if (!f->seek(segs[segIdx].off)) return false;
      segLeft = segs[segIdx].len;
      segIdx++;
    }
    uint32_t want = segLeft < sizeof(buf) ? segLeft : (uint32_t)sizeof(buf);
    int n = f->read(buf, want);
    if (n <= 0) return false;
    segLeft -= (uint32_t)n;
    bufLen = n;
    bufPos = 0;
    return true;
  }
  int next() {
    if (bufPos >= bufLen && !refill()) return -1;
    return buf[bufPos++];
  }
};

// Accumulateur LSB-first : les codes huffman arrivent MSB-first dans le flux.
struct BitR {
  BitSrc*  s;
  uint32_t acc = 0;
  int      nbits = 0;

  bool ensure(int n) {
    while (nbits < n) {
      int b = s->next();
      if (b < 0) return false;
      acc |= (uint32_t)b << nbits;
      nbits += 8;
    }
    return true;
  }
  int bits(int n) {           // n bits, valeur LSB-first ; -1 si EOF
    if (!ensure(n)) return -1;
    int v = (int)(acc & ((1u << n) - 1));
    acc >>= n;
    nbits -= n;
    return v;
  }
  void align() {
    int k = nbits & 7;
    acc >>= k;
    nbits -= k;
  }
};

// ---------------------------------------------------------------------------
// Tables huffman canoniques (RFC 1951)
// ---------------------------------------------------------------------------
struct Huff {
  uint8_t  len[288];          // longueur par symbole
  uint16_t sym[288];          // symboles triés (longueur, symbole) = ordre canonique
  uint16_t count[16] = {0};
  uint16_t firstCode[16];     // premier code canonique de chaque longueur
  int      n = 0, maxBits = 0, lbits = 0;
  int16_t  look[RAIN_LOOKUP]; // codes courts, clés canoniques paddées

  void build(const uint8_t* lengths, int n_) {
    n = n_;
    memset(count, 0, sizeof count);
    for (int i = 0; i < n; i++) {
      len[i] = lengths[i];
      if (len[i]) count[len[i]]++;
    }
    uint16_t c = 0;
    maxBits = 0;
    for (int b = 1; b < 16; b++) {
      firstCode[b] = c;
      c = (uint16_t)((c + count[b]) << 1);
      if (count[b]) maxBits = b;
    }
    int idx = 0;
    for (int b = 1; b <= 15; b++)
      for (int i = 0; i < n; i++)
        if (len[i] == b) sym[idx++] = (uint16_t)i;
    // lookup : codes <= lbits, clés canoniques paddées
    lbits = maxBits < RAIN_FAST_BITS ? maxBits : RAIN_FAST_BITS;
    for (int i = 0; i < (1 << lbits); i++) look[i] = -1;
    for (int i = 0; i < n; i++) {
      if (!len[i] || len[i] > lbits) continue;
      uint16_t code = codeOf(i);
      int step = 1 << (lbits - len[i]);
      for (int pad = 0; pad < step; pad++)
        look[(code << (lbits - len[i])) | pad] = (int16_t)((len[i] << 9) | i);
    }
  }

  // Code canonique du symbole (firstCode[len] + rang parmi les mêmes longueurs).
  uint16_t codeOf(int i) {
    uint16_t c = firstCode[len[i]];
    for (int k = 0; k < i; k++)
      if (len[k] == len[i]) c++;
    return c;
  }

  // Index du premier symbole de longueur b dans sym[].
  int firstSym(int b) {
    int idx = 0;
    for (int k = 1; k < b; k++) idx += count[k];
    return idx;
  }

  int dec(BitR& br) {
    if (!br.ensure(lbits)) return -1;
    uint32_t saveAcc = br.acc;          // état complet avant la fenêtre
    int saveNbits = br.nbits;
    uint32_t v = saveAcc & ((1u << lbits) - 1);
    uint32_t vr = 0;                    // fenêtre inversée : le code arrive MSB-first
    for (int i = 0; i < lbits; i++) vr = (vr << 1) | ((v >> i) & 1);
    int e = look[vr];
    if (e >= 0) {
      int l = e >> 9;                   // ne consommer que les bits du code
      br.acc = saveAcc >> l;
      br.nbits = saveNbits - l;
      return e & 0x1FF;
    }
    if (lbits == maxBits) return -1;    // table complète : code invalide
    // chemin lent : restaurer l'état complet, lire bit à bit
    br.acc = saveAcc;
    br.nbits = saveNbits;
    uint32_t c = 0;
    for (int b = 1; b <= maxBits; b++) {
      if (!br.ensure(1)) return -1;
      c = (c << 1) | (br.acc & 1);
      br.acc >>= 1;
      br.nbits--;
      if (count[b] && c >= firstCode[b] && c < firstCode[b] + count[b])
        return sym[firstSym(b) + (int)(c - firstCode[b])];
    }
    return -1;
  }
};

// Statiques : le décodage est mono-tâche et ces tables (~2 Ko chacune)
// feraient déborder la pile de l'ESP8266 (~4 Ko) en variables locales.
static Huff g_huffLit, g_huffDist, g_huffClh;

// ---------------------------------------------------------------------------
// Tables fixes et length/distance (RFC 1951)
// ---------------------------------------------------------------------------
static uint8_t  g_fixLit[288];
static uint8_t  g_fixDist[30];
static bool     g_fixReady = false;

static const uint16_t kLenBase[29] = {
  3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t kLenExtra[29] = {
  0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t kDistBase[30] = {
  1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t kDistExtra[30] = {
  0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static void fixedInit() {
  for (int i = 0; i < 144; i++) g_fixLit[i] = 8;
  for (int i = 144; i < 256; i++) g_fixLit[i] = 9;
  for (int i = 256; i < 280; i++) g_fixLit[i] = 7;
  for (int i = 280; i < 288; i++) g_fixLit[i] = 8;
  for (int i = 0; i < 30; i++) g_fixDist[i] = 5;
  g_fixReady = true;
}

// ---------------------------------------------------------------------------
// Sortie quantifiée : fenêtre RGB565 + assemblage pixels + crop/upscale
// ---------------------------------------------------------------------------
struct QuantOut {
  uint16_t* win;                       // 8192 pixels RGB565 (heap)
  File*     dst;
  int       w;                         // largeur de la tuile
  int       cropK, cx0, cy0;
  int       stride;
  uint32_t  pixels = 0;
  int       chan = -1;                 // -1 = filtre attendu, 0..3 = R,G,B,A
  uint8_t   r = 0, g = 0, b = 0;
  // upscale
  uint16_t  rowA[RAIN_OUT], rowB[RAIN_OUT];   // lignes source RGB565 du crop
  int       curRow = -1;
  uint8_t   x0tab[RAIN_OUT], txTab[RAIN_OUT];

  void begin(File& d, int tile, int k) {
    dst = &d;
    w = tile;
    stride = 1 + 4 * tile;
    cropK = k;
    cx0 = (tile - k) / 2;
    cy0 = (tile - k) / 2;
    // tables d'interpolation horizontale (8.8 fixe)
    int A = k - 1;
    if (A <= 0) A = 1;                  // crop 1 px : tout est le même pixel
    for (int x = 0; x < RAIN_OUT; x++) {
      long fxF = (long)x * A * 256 / 239;
      x0tab[x] = (uint8_t)(fxF >> 8);
      txTab[x] = (uint8_t)(fxF & 255);
    }
  }

  uint16_t winGet(uint32_t idx) { return win[idx & (RAIN_WINDOW_PIXELS - 1)]; }

  void byte(uint8_t v) {
    if (chan < 0) { chan = 0; r = g = b = 0; return; }   // octet de filtre (None)
    if (chan == 0) r = v;
    else if (chan == 1) g = v;
    else if (chan == 2) b = v;
    chan++;
    if (chan == 4) finishPixel();
  }

  void finishPixel() {
    uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    win[pixels & (RAIN_WINDOW_PIXELS - 1)] = c;
    int x = (int)(pixels % (uint32_t)w);
    int y = (int)(pixels / (uint32_t)w);
    if (x >= cx0 && x < cx0 + cropK && y >= cy0 && y < cy0 + cropK) {
      int relY = y - cy0;
      if (relY != curRow) {
        if (curRow >= 0) emitPair(curRow - 1);   // paire (curRow-1, curRow) complète
        memcpy(rowA, rowB, sizeof rowA);
        curRow = relY;
      }
      rowB[x - cx0] = c;
    }
    pixels++;
    chan = ((int)(pixels % (uint32_t)w) == 0) ? -1 : 0;   // fin de ligne -> filtre
    r = g = b = 0;
  }

  // Émet les lignes de sortie dont la ligne source d'appui vaut `r`
  // (interpolation entre les lignes r et r+1, rowA = r, rowB = r+1).
  void emitPair(int r) {
    if (r < 0 || cropK < 2) return;
    int A = cropK - 1;
    int oyStart = (int)(((long)r * 239 + A - 1) / A);
    int oyEnd   = (int)(((long)(r + 1) * 239 + A - 1) / A);
    if (oyEnd > RAIN_OUT) oyEnd = RAIN_OUT;
    for (int oy = oyStart; oy < oyEnd; oy++) {
      int fyF = (int)((long)oy * A * 256 / 239);
      emitRow(oy, (uint8_t)(fyF & 255), r, r + 1);
    }
  }

  // Ligne de sortie oy : bilinéaire entre les lignes sources ra/rb.
  void emitRow(int oy, uint8_t ty, int ra, int rb) {
    const uint16_t* A = (ra == rb) ? rowB : rowA;
    const uint16_t* B = rowB;
    uint8_t buf[120];
    for (int x = 0; x < RAIN_OUT; x++) {
      int x0 = x0tab[x];
      int x1 = (x0 + 1 < cropK) ? x0 + 1 : x0;
      uint8_t tx = txTab[x];
      uint16_t c = bilin(A[x0], A[x1], B[x0], B[x1], tx, ty);
      uint8_t lv = rainLevelLut(c);
      if (x & 1) buf[x >> 1] |= lv;
      else       buf[x >> 1] = lv << 4;
    }
    dst->write(buf, sizeof buf);
  }

  // Dernière ligne (oy = 239, ty = 0) : la source est la dernière ligne du crop.
  void emitLastRow() {
    uint8_t buf[120];
    for (int x = 0; x < RAIN_OUT; x++) {
      int x0 = x0tab[x];
      int x1 = (x0 + 1 < cropK) ? x0 + 1 : x0;
      uint8_t tx = txTab[x];
      uint16_t c = bilin(rowB[x0], rowB[x1], rowB[x0], rowB[x1], tx, 0);
      uint8_t lv = rainLevelLut(c);
      if (x & 1) buf[x >> 1] |= lv;
      else       buf[x >> 1] = lv << 4;
    }
    dst->write(buf, sizeof buf);
  }

  // Fin du décodage : émettre la paire restante + la dernière ligne.
  void flush() {
    if (cropK < 2) {
      // crop 1 px : toute la sortie = ce pixel
      uint8_t buf[120];
      uint8_t lv = rainLevelLut(rowB[0]);
      for (int i = 0; i < 120; i++) buf[i] = (uint8_t)((lv << 4) | lv);
      for (int oy = 0; oy < RAIN_OUT; oy++) dst->write(buf, sizeof buf);
      return;
    }
    if (curRow >= 0) emitPair(curRow - 1);
    emitLastRow();
  }

  // Interpolation bilinéaire RGB565, poids 8 bits (0..255).
  static uint16_t bilin(uint16_t p00, uint16_t p10, uint16_t p01, uint16_t p11,
                        uint8_t tx, uint8_t ty) {
    uint32_t w00 = (uint32_t)(256 - tx) * (256 - ty);
    uint32_t w10 = (uint32_t)tx * (256 - ty);
    uint32_t w01 = (uint32_t)(256 - tx) * ty;
    uint32_t w11 = (uint32_t)tx * ty;
    uint32_t r = ((p00 >> 11) * w00 + (p10 >> 11) * w10 +
                  (p01 >> 11) * w01 + (p11 >> 11) * w11 + 32768) >> 16;
    uint32_t g = (((p00 >> 5) & 63) * w00 + ((p10 >> 5) & 63) * w10 +
                  ((p01 >> 5) & 63) * w01 + ((p11 >> 5) & 63) * w11 + 32768) >> 16;
    uint32_t b = ((p00 & 31) * w00 + (p10 & 31) * w10 +
                  (p01 & 31) * w01 + (p11 & 31) * w11 + 32768) >> 16;
    return (uint16_t)((r << 11) | (g << 5) | b);
  }
};

// Statique pour la même raison (rowA/rowB/tables ~1,5 Ko).
static QuantOut g_q;

// ---------------------------------------------------------------------------
// Copie back-référence : distance/longueur en octets bruts. Chaque octet de
// sortie est reconstruit canal par canal depuis le pixel source (le filtre
// None garantit qu'aucun dé-filtrage n'est nécessaire).
// ---------------------------------------------------------------------------
static void copyBytes(QuantOut& q, int d, int l) {
  for (int i = 0; i < l; i++) {
    uint32_t p = q.pixels * 4 + (q.chan >= 0 ? (uint32_t)q.chan : 0)
               + q.pixels / (uint32_t)q.w + (q.chan >= 0 ? 1 : 0);
    if (q.chan < 0) { q.byte(0); continue; }   // position de filtre
    uint32_t src = p - (uint32_t)d;
    uint32_t sk = src / (uint32_t)q.stride + 1;   // filtres à positions <= src
    uint32_t pix = (src - sk) / 4;
    int chan = (int)((src - sk) % 4);             // 0=R, 1=G, 2=B, 3=A
    uint16_t c;
    if (pix == q.pixels) {
      // le pixel en cours : ses canaux déjà émis vivent dans les registres
      c = (uint16_t)(((q.r >> 3) << 11) | ((q.g >> 2) << 5) | (q.b >> 3));
    } else {
      c = q.winGet(pix);
    }
    uint8_t ch;
    if (chan == 0)      ch = (uint8_t)(((c >> 11) << 3) | ((c >> 11) >> 2));
    else if (chan == 1) ch = (uint8_t)((((c >> 5) & 63) << 2) | ((c >> 5) >> 4));
    else if (chan == 2) ch = (uint8_t)(((c & 31) << 3) | ((c & 31) >> 2));
    else                ch = (c == 0) ? 0 : 255;  // A : transparent ssi RGB noir
    q.byte(ch);
  }
}

// ---------------------------------------------------------------------------
// Inflate (RFC 1951) — blocs stored/fixed/dynamic
// ---------------------------------------------------------------------------
static bool inflate(BitR& br, QuantOut& q) {
  Huff& lit = g_huffLit;
  Huff& dist = g_huffDist;
  while (true) {
    int bfinal = br.bits(1);
    int btype = br.bits(2);
    if (bfinal < 0 || btype < 0) return false;

    Huff* L;
    Huff* D;
    if (btype == 0) {
      br.align();
      int ln = br.bits(16);
      int nln = br.bits(16);
      if (ln < 0 || nln < 0 || ln != (nln ^ 0xFFFF)) return false;
      for (int i = 0; i < ln; i++) {
        int b = br.bits(8);
        if (b < 0) return false;
        q.byte((uint8_t)b);
      }
    } else {
      if (btype == 1) {
        if (!g_fixReady) fixedInit();
        lit.build(g_fixLit, 288);
        dist.build(g_fixDist, 30);
        L = &lit;
        D = &dist;
      } else if (btype == 2) {
        int hlit = br.bits(5);
        int hdist = br.bits(5);
        int hclen = br.bits(4);
        if (hlit < 0 || hdist < 0 || hclen < 0) return false;
        hlit += 257;
        hdist += 1;
        hclen += 4;
        static const uint8_t kOrder[19] = {
          16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
        };
        uint8_t clen[19];
        memset(clen, 0, sizeof clen);
        for (int i = 0; i < hclen; i++) {
          int v = br.bits(3);
          if (v < 0) return false;
          clen[kOrder[i]] = (uint8_t)v;
        }
        Huff& clh = g_huffClh;
        clh.build(clen, 19);
        uint8_t lengths[288 + 32];
        int nl = 0;
        while (nl < hlit + hdist) {
          int s = clh.dec(br);
          if (s < 0) return false;
          if (s < 16) {
            lengths[nl++] = (uint8_t)s;
          } else if (s == 16) {
            int r = br.bits(2) + 3;
            if (r < 3) return false;
            uint8_t pv = lengths[nl - 1];
            for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = pv;
          } else if (s == 17) {
            int r = br.bits(3) + 3;
            if (r < 3) return false;
            for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = 0;
          } else {
            int r = br.bits(7) + 11;
            if (r < 11) return false;
            for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = 0;
          }
        }
        lit.build(lengths, hlit);
        dist.build(lengths + hlit, hdist);
        L = &lit;
        D = &dist;
      } else {
        return false;
      }

      while (true) {
        int s = L->dec(br);
        if (s < 0) return false;
        if (s < 256) {
          q.byte((uint8_t)s);
        } else if (s == 256) {
          break;
        } else {
          int sym = s - 257;
          int le = kLenBase[sym] + br.bits(kLenExtra[sym]);
          int dsym = D->dec(br);
          if (dsym < 0) return false;
          int d = kDistBase[dsym] + br.bits(kDistExtra[dsym]);
          copyBytes(q, d, le);
        }
      }
    }
    if (bfinal) return true;
  }
}

// ---------------------------------------------------------------------------
// Entrée principale
// ---------------------------------------------------------------------------
bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK) {
  if (!g_lutReady) rainLevelLutBuild();

  // --- signature + parcours des chunks --------------------------------
  uint8_t sig[8];
  if (src.read(sig, 8) != 8) return false;
  if (memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) return false;

  int      w = 0, h = 0, bpp = 0, ctype = 0, interlace = 0;
  IdatSeg  segs[RAIN_MAX_IDAT];
  int      segCount = 0;
  uint32_t idatLen = 0;

  uint8_t lenBuf[4], type[4];
  for (;;) {
    if (src.read(lenBuf, 4) != 4) return false;
    uint32_t len = ((uint32_t)lenBuf[0] << 24) | ((uint32_t)lenBuf[1] << 16) |
                   ((uint32_t)lenBuf[2] << 8) | lenBuf[3];
    if (src.read(type, 4) != 4) return false;
    if (memcmp(type, "IHDR", 4) == 0) {
      uint8_t ihdr[13];
      if (src.read(ihdr, 13) != 13) return false;
      w = ((int)ihdr[0] << 24) | ((int)ihdr[1] << 16) | ((int)ihdr[2] << 8) | ihdr[3];
      h = ((int)ihdr[4] << 24) | ((int)ihdr[5] << 16) | ((int)ihdr[6] << 8) | ihdr[7];
      bpp = ihdr[8];
      ctype = ihdr[9];
      interlace = ihdr[12];
      src.seek(src.position() + 4);   // CRC (les 13 octets sont déjà lus)
    } else if (memcmp(type, "IDAT", 4) == 0) {
      if (segCount < RAIN_MAX_IDAT) {
        segs[segCount].off = src.position();
        segs[segCount].len = len;
        segCount++;
      }
      idatLen += len;
      src.seek(src.position() + len + 4);
    } else if (memcmp(type, "IEND", 4) == 0) {
      break;
    } else {
      src.seek(src.position() + len + 4);
    }
  }

  // Les tuiles radar sont RGBA 8-bit non entrelacées ; rien d'autre n'est
  // attendu (les tuiles indexées 4-bit servies à z>=8 sont des cartes).
  if (w != tileSize || h != tileSize || bpp != 8 || ctype != 6 || interlace != 0)
    return false;
  if (idatLen == 0 || idatLen < 6) return false;

  // --- décompression --------------------------------------------------
  if (cropK < 1 || cropK > RAIN_OUT) cropK = RAIN_OUT;

  uint16_t* win = (uint16_t*)malloc(RAIN_WINDOW_PIXELS * 2);
  if (!win) return false;
  memset(win, 0, RAIN_WINDOW_PIXELS * 2);

  QuantOut& q = g_q;
  q.win = win;
  q.begin(dst, tileSize, cropK);

  BitSrc bs;
  bs.f = &src;
  bs.segs = segs;
  bs.segCount = segCount;
  BitR br;
  br.s = &bs;

  br.bits(16);   // en-tête zlib (CMF/FLG)
  bool ok = inflate(br, q);
  if (ok && q.pixels == (uint32_t)tileSize * (uint32_t)tileSize)
    q.flush();
  else
    ok = false;

  free(win);
  return ok;
}
