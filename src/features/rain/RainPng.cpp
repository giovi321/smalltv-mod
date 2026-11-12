// RainPng.cpp — décodeur PNG embarqué (tuiles radar RainViewer 256 px RGBA).
//
// Pourquoi pas PNGdec : ses buffers statiques (~38 Ko) dépassent le heap de
// l'ESP8266. Les tuiles RainViewer utilisent le filtre « None » sur toutes
// leurs lignes (vérifié), donc aucun dé-filtrage n'est nécessaire.
//
// Fenêtre LZ : 8192 pixels × 1 octet = indice kRainColors (6 bits) + classe
// d'alpha (2 bits, {0,85,170,255}). Sans l'alpha en histoire, une copie
// reconstruisait alpha 255 : les pixels mixtes (littéral + copie — 99 % du
// flux) étaient trop clairs ou trop sombres sur les nuages semi-transparents
// (vécu sur l'Ultra : écarts nets vs le rendu RainViewer). Les octets copiés
// sont reconstruits en brut (couleur × 255/alpha en facteurs entiers), puis
// la prémultiplication de finishPixel redonne la couleur affichée. Un pixel
// entièrement copié depuis la même entrée affiche directement la couleur de
// la source, sans re-quantifier (la LUT n'est pas idempotente sur toutes les
// couleurs de la palette).
// La profondeur de 8192 pixels couvre la fenêtre deflate native (32 Ko
// d'octets bruts ≈ 8184 pixels) : la réduire casse le décodage (89 %
// d'erreurs à 4096, vécu).
// La fenêtre est allouée par l'appelant, une fois par fetch (voir RainPng.h).
// Le malloc de 8 Ko exigeait 4 Ko de heap libérés en passant la LUT de
// couleurs en flash (rain_clut.h, précalculée par tools/make_rain_clut.py —
// le plus gros bloc libre mesuré au décodage était de 7232 o sur l'Ultra).
#include "RainPng.h"
#include "RainData.h"
#include "rain_clut.h"

#define RAIN_FAST_BITS     8           // lookup huffman rapide (2^8 entrées)
                                       // 9 bits = décodage plus rapide mais
                                       // 1,5 Ko de BSS en plus ; 8 bits
                                       // libère cette marge pour le pic du
                                       // fetch (abort OOM vécu sur l'Ultra)
#define RAIN_LOOKUP        (1 << RAIN_FAST_BITS)
#define RAIN_OUT           240         // taille de sortie (écran)
#define RAIN_MAX_IDAT      8           // chunks IDAT par fichier (2 en pratique)

// Classes d'alpha (2 bits) stockées avec l'indice de couleur : valeur réelle
// et facteur de reconstruction brut (×255/valeur : ×1, ×3/2, ×3, entiers).
static const uint8_t kAlphaVal[4] = {0, 85, 170, 255};
static uint8_t alphaClass(uint8_t a) {
  return a < 43 ? 0 : (a < 128 ? 1 : (a < 213 ? 2 : 3));
}

// ---------------------------------------------------------------------------
// Couleur prémultipliée RGB565 -> indice dans kRainColors (LUT R5G5B2 de
// 4096 entrées en flash, précalculée par tools/make_rain_clut.py). Clé
// R5G5B2 : les gris sombres de la couche nuageuse diffèrent surtout en R/G,
// préservés ; les bleus de la rampe pluie diffèrent surtout en G.
// ---------------------------------------------------------------------------
static uint8_t g_cols[RAIN_COLOR_COUNT][3];   // copie RAM de kRainColors (copies LZ)
static bool    g_colsReady = false;

void rainColorTableInit() {
  for (uint8_t k = 0; k < RAIN_COLOR_COUNT; k++)
    memcpy_P(g_cols[k], kRainColors[k], 3);
  g_colsReady = true;
}

uint8_t rainColorIndex(uint16_t c) {
  if (c == 0) return 0;   // transparent
  uint16_t key = (uint16_t)(((c >> 11) << 7) | ((((c >> 5) & 63) >> 1) << 2) | ((c & 31) >> 3));
  return pgm_read_byte(&kRainClut[key]);
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

// Buffer de lecture du BitSrc : statique, 512 B sur la pile feraient déborder
// le cont stack de l'ESP8266 (~688 B de marge) pendant le décodage (vécu).
static uint8_t g_srcBuf[512];

struct BitSrc {
  File*          f;
  const IdatSeg* segs = nullptr;
  int            segCount = 0;
  int            segIdx = 0;
  uint32_t       segLeft = 0;    // octets restants dans le segment courant
  int            bufPos = 0, bufLen = 0;

  bool refill() {
    while (segLeft == 0) {
      if (segIdx >= segCount) return false;
      if (!f->seek(segs[segIdx].off)) return false;
      segLeft = segs[segIdx].len;
      segIdx++;
    }
    uint32_t want = segLeft < sizeof(g_srcBuf) ? segLeft : (uint32_t)sizeof(g_srcBuf);
    int n = f->read(g_srcBuf, want);
    if (n <= 0) return false;
    segLeft -= (uint32_t)n;
    bufLen = n;
    bufPos = 0;
    return true;
  }
  int next() {
    if (bufPos >= bufLen && !refill()) return -1;
    return g_srcBuf[bufPos++];
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
// Sortie quantifiée : fenêtre d'entrées + assemblage pixels + crop/upscale
// ---------------------------------------------------------------------------
struct QuantOut {
  uint8_t*  win;                       // 8192 entrées : indice<<2 | classe alpha
  File*     dst;
  int       w;                         // largeur de la tuile
  int       cropK, cx0, cy0;
  int       stride;
  uint32_t  pixels = 0;
  int       chan = -1;                 // -1 = filtre attendu, 0..3 = R,G,B,A
  uint16_t  r = 0, g = 0, b = 0;       // canaux bruts (reconstruction ×3 : ≤765)
  uint8_t   a = 0;
  uint8_t   histE = 0xFF;              // entrée de fenêtre des octets copiés :
                                       // 0xFF = aucun, 0xFE = sources mixtes
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
    // g_q est statique : remise à zéro de l'état par décodage (les
    // initialiseurs de membre ne jouent qu'au premier). Sans ça le 2e
    // décodage repart de pixels = 65536 (compte 2x/3x, vécu sur l'Ultra).
    pixels = 0;
    chan = -1;
    r = g = b = 0;
    a = 0;
    histE = 0xFF;
    curRow = -1;
    // tables d'interpolation horizontale (8.8 fixe)
    int A = k - 1;
    if (A <= 0) A = 1;                  // crop 1 px : tout est le même pixel
    for (int x = 0; x < RAIN_OUT; x++) {
      long fxF = (long)x * A * 256 / 239;
      x0tab[x] = (uint8_t)(fxF >> 8);
      txTab[x] = (uint8_t)(fxF & 255);
    }
  }

  void winSet(uint32_t n, uint8_t v) { win[n & (RAIN_WINDOW_PIXELS - 1)] = v; }

  uint8_t winGet(uint32_t idx) { return win[idx & (RAIN_WINDOW_PIXELS - 1)]; }

  // Octet littéral : RGBA brut du flux déflaté (prémultiplié par finishPixel
  // une fois l'alpha connu).
  void byte(uint16_t v) {
    if (chan < 0) { chan = 0; r = g = b = 0; a = 0; histE = 0xFF; return; }  // filtre (None)
    histE = 0xFE;                       // brut : le pixel n'est pas « tout-histoire »
    if (chan == 0) r = v;
    else if (chan == 1) g = v;
    else if (chan == 2) b = v;
    else a = (uint8_t)v;
    chan++;
    if (chan == 4) finishPixel();
  }

  // Octet copié depuis la fenêtre (entrée e = indice<<2 | classe alpha,
  // sch = canal SOURCE — peut différer du canal courant si la copie est
  // désalignée ; la destination est le canal courant) : valeur reconstruite
  // en brut (couleur × 255/alpha), la prémultiplication de finishPixel
  // redonne la couleur affichée.
  void byteHist(uint8_t e, int sch) {
    if (chan < 0) { chan = 0; r = g = b = 0; a = 0; histE = 0xFF; return; }  // filtre
    if (histE == 0xFF) histE = e;
    else if (histE != e) histE = 0xFE;  // octets de plusieurs sources : pixel mixte
    uint8_t idx = e >> 2;
    uint8_t av = kAlphaVal[e & 3];
    uint16_t v;
    if (sch == 3) {
      v = (idx == 0) ? 0 : av;
    } else {
      v = g_cols[idx][sch];
      if (av == 170) v += v >> 1;       // ×255/170 = ×3/2
      else if (av == 85) v *= 3;        // ×255/85  = ×3
    }
    if (chan == 0) r = v;
    else if (chan == 1) g = v;
    else if (chan == 2) b = v;
    else a = (uint8_t)v;
    chan++;
    if (chan == 4) finishPixel();
  }

  void finishPixel() {
    uint8_t idx;
    uint16_t c;
    if (histE != 0xFF && histE != 0xFE) {
      // Pixel entièrement copié depuis la même entrée : la couleur affichée
      // est exactement celle de la source — sans re-quantifier (la LUT n'est
      // pas idempotente sur toutes les couleurs de la palette, vécu).
      idx = histE >> 2;
      c = (uint16_t)(((g_cols[idx][0] >> 3) << 11) |
                     ((g_cols[idx][1] >> 2) << 5) |
                     (g_cols[idx][2] >> 3));
      winSet(pixels, histE);
    } else {
      // Prémultiplication sur noir (le fond de l'écran) : x/255 ~ (x + 1 + x>>8)>>8
      uint32_t rr = ((uint32_t)r * a + 1 + ((uint32_t)r * a >> 8)) >> 8;
      uint32_t gg = ((uint32_t)g * a + 1 + ((uint32_t)g * a >> 8)) >> 8;
      uint32_t bb = ((uint32_t)b * a + 1 + ((uint32_t)b * a >> 8)) >> 8;
      c = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
      idx = rainColorIndex(c);
      winSet(pixels, (uint8_t)((idx << 2) | alphaClass(a)));
    }
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
    a = 0;
    histE = 0xFF;
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
    static uint8_t buf[RAIN_OUT];   // statique : économie de pile (voir g_srcBuf)
    for (int x = 0; x < RAIN_OUT; x++) {
      int x0 = x0tab[x];
      int x1 = (x0 + 1 < cropK) ? x0 + 1 : x0;
      uint8_t tx = txTab[x];
      uint16_t c = bilin(A[x0], A[x1], B[x0], B[x1], tx, ty);
      buf[x] = rainColorIndex(c);
    }
    dst->write(buf, sizeof buf);
  }

  // Dernière ligne (oy = 239, ty = 0) : la source est la dernière ligne du crop.
  void emitLastRow() {
    static uint8_t buf[RAIN_OUT];   // statique : économie de pile (voir g_srcBuf)
    for (int x = 0; x < RAIN_OUT; x++) {
      int x0 = x0tab[x];
      int x1 = (x0 + 1 < cropK) ? x0 + 1 : x0;
      uint8_t tx = txTab[x];
      uint16_t c = bilin(rowB[x0], rowB[x1], rowB[x0], rowB[x1], tx, 0);
      buf[x] = rainColorIndex(c);
    }
    dst->write(buf, sizeof buf);
  }

  // Fin du décodage : émettre la paire restante + la dernière ligne.
  void flush() {
    if (cropK < 2) {
      // crop 1 px : toute la sortie = ce pixel
      static uint8_t buf[RAIN_OUT];
      uint8_t idx = rainColorIndex(rowB[0]);
      for (int i = 0; i < RAIN_OUT; i++) buf[i] = idx;
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
// sortie est reconstruit canal par canal depuis l'entrée de fenêtre du pixel
// source (le filtre None garantit qu'aucun dé-filtrage n'est nécessaire).
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
    if (pix == q.pixels) {
      // Le pixel en cours : ses canaux déjà émis vivent en brut dans les
      // registres (le flux déflaté contient du RGBA brut ; la prémultipli-
      // cation se fait dans finishPixel une fois l'alpha arrivé). Son alpha
      // est déjà arrivé quand une copie le lit (la source est en arrière).
      if (chan == 0)      q.byte(q.r);
      else if (chan == 1) q.byte(q.g);
      else if (chan == 2) q.byte(q.b);
      else                q.byte(q.a);
    } else {
      q.byteHist(q.winGet(pix), chan);
    }
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
// Codes d'échec (dans les commentaires des appels à fail) : 1 = lecture
// courte, 2 = pas un PNG, 3 = parcours de chunks impossible, 4 = IHDR
// inattendu, 5 = pas d'IDAT, 6 = fenêtre absente (allouée par l'appelant),
// 7 = flux défectueux, 8 = compte de pixels.
static bool fail(int) { return false; }

bool rainDecodeToFs(File& src, File& dst, int tileSize, int cropK, uint8_t* win) {
  if (!g_colsReady) rainColorTableInit();

  // --- signature + parcours des chunks --------------------------------
  uint8_t sig[8];
  if (src.read(sig, 8) != 8) return fail(1);
  if (memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) return fail(2);

  int      w = 0, h = 0, bpp = 0, ctype = 0, interlace = 0;
  IdatSeg  segs[RAIN_MAX_IDAT];
  int      segCount = 0;
  uint32_t idatLen = 0;

  uint8_t lenBuf[4], type[4];
  for (;;) {
    if (src.read(lenBuf, 4) != 4) return fail(3);
    uint32_t len = ((uint32_t)lenBuf[0] << 24) | ((uint32_t)lenBuf[1] << 16) |
                   ((uint32_t)lenBuf[2] << 8) | lenBuf[3];
    if (src.read(type, 4) != 4) return fail(3);
    if (memcmp(type, "IHDR", 4) == 0) {
      uint8_t ihdr[13];
      if (src.read(ihdr, 13) != 13) return fail(3);
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
    return fail(4);
  if (idatLen == 0 || idatLen < 6) return fail(5);

  // --- décompression --------------------------------------------------
  if (cropK < 1 || cropK > RAIN_OUT) cropK = RAIN_OUT;

  if (!win) return fail(6);

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
    ok = fail(ok ? 8 : 7);   // 7 = flux défectueux, 8 = compte de pixels

  return ok;
}
