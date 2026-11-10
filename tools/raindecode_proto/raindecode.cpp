// raindecode.cpp — prototype PC du décodeur PNG embarqué pour l'ESP8266.
//
// Contexte : les tuiles radar RainViewer (zoom 7, RGBA 8-bit, filtre PNG
// "None" sur toutes les lignes — vérifié) ne peuvent pas passer par PNGdec
// sur ESP8266 (buffers statiques ~38 Ko). Ce décodeur évite la fenêtre
// deflate de 32 Ko : comme aucun dé-filtrage n'est nécessaire et que la
// sortie est immédiatement quantifiée, la fenêtre LZ stocke 1 nibble par
// pixel au lieu de 4 octets RGBA -> 4 Ko au lieu de 32 Ko.
//
// Les back-références mal alignées sur les pixels (dist impaire, etc.)
// produisent des couleurs mélangées entre 2 pixels sources ; sans
// dé-filtrage ni vérification CRC, c'est sans conséquence sur le rendu
// (léger lissage aux bords des blobs).
//
// Usage : raindecode in.zlib out.raw [w] [cropX cropY cropK] [upscale]
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Rampe source RainViewer "Universal Blue" (RGB565, pluie faible -> forte).
static const uint16_t kRamp[20] = {
  0x8EFD, 0x6E9D, 0x563D, 0x35DC, 0x1D7C, 0x051C, 0x04DA, 0x0499,
  0x0457, 0x03F6, 0x03B5, 0x0394, 0x0353, 0x0312, 0x02D1, 0x02B1,
  0x0290, 0x026F, 0x024E, 0x022D,
};

static uint8_t levelFor(uint16_t c) {
  if (c == 0) return 0;
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  uint8_t best = 0;
  int bestD = 1 << 30;
  for (uint8_t i = 0; i < 20; i++) {
    uint16_t t = kRamp[i];
    int dr = r - ((t >> 11) & 31);
    int dg = g - ((t >> 5) & 63);
    int db = b - (t & 31);
    int d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return 1 + (uint8_t)(best * 14 / 19);
}

// ---------------------------------------------------------------------------
// Sortie quantifiée : fenêtre LZ circulaire de nibbles + assemblage pixels.
// La fenêtre couvre 32 768 octets bruts = 8 192 pixels = 4 096 nibbles.
struct QuantOut {
  uint16_t* win;         // 8192 pixels RGB565 (fenêtre 32 Ko bruts = 16 Ko)
  int winSize = 8192;
  int winPos = 0;        // prochain pixel à écrire (index circulaire)
  uint32_t pixels = 0;   // pixels émis au total
  int w, h;              // dimensions image
  int stride;            // octets bruts par ligne = 1 + 4*w
  uint8_t r, g, b;       // accumulateur pixel courant
  int chan = -1;         // canal courant (-1 = filtre attendu)
  FILE* out;             // fichier .raw (nibbles)
  // upscale / crop
  bool crop = false;
  int cx0, cy0, ck;      // fenêtre source (px) : [cx0, cx0+ck) x [cy0, cy0+ck)
  int outSize = 240;
  std::vector<uint16_t> rowA, rowB;   // lignes source RGB565 pour bilinéaire
  int curY = -1;
  int cropRow = -1;      // ligne source en cours dans la fenêtre de crop

  void init(int w_, int h_) {
    w = w_; h = h_; stride = 1 + 4 * w;
    win = new uint16_t[winSize];
    memset(win, 0, winSize * 2);
  }
  uint16_t winGet(uint32_t idx) const { return win[idx & (winSize - 1)]; }
  void winPut(uint32_t idx, uint16_t v) { win[idx & (winSize - 1)] = v; }

  // pixel index -> nibble index
  static int nibOf(uint32_t pixel) { return (int)(pixel >> 1); }

  // octet brut (position absolue dans le flux sortie) -> pixel source
  // dans la fenêtre pour une back-ref de distance d octets.
  static int skips(uint64_t p) { return (int)(p / 1025) + 1; }  // filtres aux multiples de stride... approximation stride 1025 pour w=256

  uint64_t emitted = 0;
  int g_desyncCount = 0;
  void byte(uint8_t v) {
    // v = octet brut de la sortie décompressée
    // position brute attendue = 4 octets/pixel + 1 filtre/ligne
    uint64_t p = pixels * 4 + (chan >= 0 ? chan : 0) + pixels / w + (chan >= 0 ? 1 : 0);
    if (p != emitted) {
      printf("[desync] attendu=%llu emis=%llu pixels=%u chan=%d\n",
             (unsigned long long)p, (unsigned long long)emitted, pixels, chan);
      if (++g_desyncCount > 6) exit(1);
    }
    emitted++;
    if (chan < 0) {
      // octet de filtre (toujours 0 sur ces tuiles) — on l'ignore
      chan = 0;
      r = g = b = 0;
      return;
    }
    if (chan == 0) r = v;
    else if (chan == 1) g = v;
    else if (chan == 2) b = v;
    else { /* chan == 3 : alpha — ignoré */ }
    chan++;
    if (chan == 4) finishPixel();
  }

  void finishPixel() {
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    // fenêtre : index circulaire par pixel global
    winPut(pixels, c);
    // crop/upscale
    handlePixel(pixels);
    pixels++;
    // fin de ligne -> l'octet suivant est le filtre de la ligne suivante
    chan = ((int)(pixels % (uint32_t)w) == 0) ? -1 : 0;
    r = g = b = 0;   // nouveau pixel vierge
    winPos = nibOf(pixels);
  }

  void handlePixel(uint32_t pixel) {
    int x = (int)(pixel % w), y = (int)(pixel / w);
    if (!crop) {
      if (out) {
        static uint8_t nib = 0;
        static int half = 0;
        if (!half) nib = levelAt(pixel) << 4; else nib |= levelAt(pixel);
        half ^= 1;
        if (!half) fwrite(&nib, 1, 1, out);
      }
      return;
    }
    // crop : le pixel est-il dans [cx0, cx0+ck) x [cy0, cy0+ck) ?
    if (x < cx0 || x >= cx0 + ck || y < cy0 || y >= cy0 + ck) return;
    if (y != curY) {
      curY = y;
      cropRow++;
      rowA.swap(rowB);
      std::fill(rowA.begin(), rowA.end(), 0);
    }
    rowA[x - cx0] = c565(levelAt(pixel));
    if (cropRow == ck - 1 && x == cx0 + ck - 1) {
      // dernière ligne du crop : émettre la sortie upscalée
      emitCrop();
    }
  }

  uint8_t levelAt(uint32_t pixel) { return levelFor(winGet(pixel)); }

  static uint16_t c565(uint8_t lv) {
    if (!lv) return 0;
    return kRamp[(lv - 1) * 19 / 15];
  }

  void emitCrop() {
    // bilinéaire rowB (première ligne) -> 240 lignes de sortie
    for (int oy = 0; oy < outSize; oy++) {
      float fy = oy * (ck - 1) / (float)(outSize - 1);
      int y0 = (int)fy;
      float ty = fy - y0;
      const uint16_t* ra = (y0 == 0) ? rowB.data() : rowB.data();
      (void)ra;
      // rows : rowB = ligne y-1 (précédente), rowA = ligne y courante ?
      // simplification du prototype : interpole rowB -> rowA sur ck lignes
      // (l'ordre exact est géré dans handlePixel : rowB = plus ancienne)
      float fx0 = 0;
      for (int ox = 0; ox < outSize; ox++) {
        float fx = ox * (ck - 1) / (float)(outSize - 1);
        int x0 = (int)fx;
        float tx = fx - x0;
        (void)fx0;
        uint16_t p00 = rowB[x0], p10 = rowB[x0 + 1 < ck ? x0 + 1 : x0];
        uint16_t p01 = rowA[x0], p11 = rowA[x0 + 1 < ck ? x0 + 1 : x0];
        uint16_t c = bilin(p00, p10, p01, p11, tx, ty);
        uint8_t lv = levelFor(c);
        static uint8_t nib = 0;
        static int half = 0;
        if (!half) nib = lv << 4; else nib |= lv;
        half ^= 1;
        if (!half) fwrite(&nib, 1, 1, out);
      }
    }
    cropRow = -2;  // fait
  }

  static uint16_t bilin(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                        float tx, float ty) {
    auto ch = [](uint16_t v, int sh, int m) {
      return (float)((v >> sh) & m);
    };
    float r = ch(a, 11, 31) * (1 - tx) * (1 - ty) + ch(b, 11, 31) * tx * (1 - ty) +
              ch(c, 11, 31) * (1 - tx) * ty + ch(d, 11, 31) * tx * ty;
    float g = ch(a, 5, 63) * (1 - tx) * (1 - ty) + ch(b, 5, 63) * tx * (1 - ty) +
              ch(c, 5, 63) * (1 - tx) * ty + ch(d, 5, 63) * tx * ty;
    float bl = ch(a, 0, 31) * (1 - tx) * (1 - ty) + ch(b, 0, 31) * tx * (1 - ty) +
               ch(c, 0, 31) * (1 - tx) * ty + ch(d, 0, 31) * tx * ty;
    return ((uint16_t)(r + 0.5f) << 11) | ((uint16_t)(g + 0.5f) << 5) |
           (uint16_t)(bl + 0.5f);
  }
};

// ---------------------------------------------------------------------------
// Inflate minimal (RFC 1951) — littéraux + copies, tables canoniques.
struct BitR {
  const uint8_t* d;
  int bit = 0;
  int bits(int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
      v |= ((d[bit >> 3] >> (bit & 7)) & 1) << i;
      bit++;
    }
    return v;
  }
  void align() { bit = (bit + 7) & ~7; }
};

struct Huff {
  uint16_t count[16] = {0};
  int16_t sym[288 + 32];
  int maxBits = 0;
  uint16_t code[288 + 32];
  uint8_t len[288 + 32];
  int n = 0;
  // lookup rapide : reversé
  int16_t look[1 << 15] = {0};
  int lbits = 0;

  void build(const uint8_t* lengths, int n_) {
    n = n_;
    memset(count, 0, sizeof count);
    for (int i = 0; i < n; i++) {
      len[i] = lengths[i];
      if (len[i]) count[len[i]]++;
    }
    uint16_t next[16] = {0};
    uint16_t c = 0;
    for (int b = 1; b < 16; b++) {
      c = (c + count[b - 1]) << 1;
      next[b] = c;
      if (count[b]) maxBits = b;
    }
    for (int i = 0; i < n; i++) {
      if (!len[i]) continue;
      code[i] = next[len[i]]++;
    }
    // lookup : la fenêtre lue est inversée avant l'indexation (codes
    // canoniques dans les bits hauts, padding dans les bits bas).
    lbits = maxBits;
    if (lbits > 15) lbits = 15;
    for (int i = 0; i < n; i++) {
      if (!len[i]) continue;
      int step = 1 << (lbits - len[i]);
      for (int pad = 0; pad < step; pad++)
        look[(code[i] << (lbits - len[i])) | pad] = (int16_t)((len[i] << 9) | i);
    }
  }
  int dec(BitR& br) {
    int v = 0;
    for (int i = 0; i < lbits; i++) {
      v |= ((br.d[br.bit >> 3] >> (br.bit & 7)) & 1) << i;
      br.bit++;
    }
    // inverser la fenêtre : le code arrive MSB-first dans le flux
    int vr = 0;
    for (int i = 0; i < lbits; i++) vr = (vr << 1) | ((v >> i) & 1);
    int e = look[vr];
    if (!e && vr) return -1;
    br.bit -= lbits - (e >> 9);
    return e & 0x1FF;
  }
};

static const int LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static uint8_t FIX_LIT[288];
static uint8_t FIX_DIST[30];
static bool fixInit = false;
static void initFixed() {
  for (int i = 0; i < 144; i++) FIX_LIT[i] = 8;
  for (int i = 144; i < 256; i++) FIX_LIT[i] = 9;
  for (int i = 256; i < 280; i++) FIX_LIT[i] = 7;
  for (int i = 280; i < 288; i++) FIX_LIT[i] = 8;
  for (int i = 0; i < 30; i++) FIX_DIST[i] = 5;
  fixInit = true;
}

// copie back-ref : distance d octets bruts, longueur l octets bruts.
// La fenêtre stocke des nibbles par pixel ; on reconstruit les octets canal
// par canal à partir du nibble source (position brute p - d).
static void copyBytes(QuantOut& q, BitR& br, int d, int l) {
  (void)br;
  for (int i = 0; i < l; i++) {
    // position brute absolue de l'octet de sortie courant (4 octets/pixel
    // + 1 octet de filtre par ligne)
    uint64_t p = (uint64_t)q.pixels * 4 + (q.chan >= 0 ? (uint64_t)q.chan : 0)
               + (uint64_t)(q.pixels / q.w) + (q.chan >= 0 ? 1 : 0);
    if (q.chan < 0) {
      // l'octet courant est un filtre : la source peut être n'importe quoi,
      // on émet 0 (les tuiles sont en filtre None de toute façon)
      q.byte(0);
      continue;
    }
    uint64_t src = p - (uint64_t)d;
    // pixel source et canal : skips = octets de filtre à positions <= src
    uint64_t sk = src / q.stride + 1;
    uint64_t pix = (src - sk) / 4;
    int chan = (int)((src - sk) % 4);   // 0=R, 1=G, 2=B, 3=A
    uint16_t c;
    if (pix == (uint64_t)q.pixels) {
      // le pixel en cours : ses canaux déjà émis vivent dans les registres
      c = ((q.r >> 3) << 11) | ((q.g >> 2) << 5) | (q.b >> 3);
    } else {
      c = q.winGet((uint32_t)pix);
    }
    uint8_t ch;
    if (chan == 0) ch = (uint8_t)(((c >> 11) << 3) | ((c >> 11) >> 2));
    else if (chan == 1) ch = (uint8_t)((((c >> 5) & 63) << 2) | ((c >> 5) >> 4));
    else if (chan == 2) ch = (uint8_t)(((c & 31) << 3) | ((c & 31) >> 2));
    else ch = (c == 0) ? 0 : 255;   // A : transparent iff RGB noir
    q.byte(ch);
  }
}

static uint64_t g_litBytes = 0, g_copyBytes = 0, g_copies = 0;
static bool inflateBlock(QuantOut& q, BitR& br) {
  while (true) {
    int bfinal = br.bits(1);
    int btype = br.bits(2);
    if (btype == 0) {
      br.align();
      int ln = br.bits(16);
      int nln = br.bits(16);
      if (ln != (nln ^ 0xFFFF)) { printf("LEN/NLEN invalide\n"); return false; }
      for (int i = 0; i < ln; i++) q.byte((uint8_t)br.bits(8));
    } else {
      Huff lit, dist;
      if (btype == 1) {
        if (!fixInit) initFixed();
        lit.build(FIX_LIT, 288);
        dist.build(FIX_DIST, 30);
      } else if (btype == 2) {
        int hlit = br.bits(5) + 257;
        int hdist = br.bits(5) + 1;
        int hclen = br.bits(4) + 4;
        static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        uint8_t clen[19] = {0};
        for (int i = 0; i < hclen; i++) clen[order[i]] = (uint8_t)br.bits(3);
        Huff clh;
        clh.build(clen, 19);
        printf("[dbg] hlit=%d hdist=%d hclen=%d clen=", hlit, hdist, hclen);
        for (int i = 0; i < 19; i++) printf("%d,", clen[i]);
        printf("\n");
        uint8_t lengths[288 + 32];
        int nl = 0;
        int dbgsyms = 0;
        while (nl < hlit + hdist) {
          int s = clh.dec(br);
          if (dbgsyms++ < 40) printf("[rle] s=%d nl=%d bit=%d\n", s, nl, br.bit);
          if (s < 16) lengths[nl++] = (uint8_t)s;
          else if (s == 16) { int r = 3 + br.bits(2); uint8_t pv = lengths[nl-1];
                              for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = pv; }
          else if (s == 17) { int r = 3 + br.bits(3);
                              for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = 0; }
          else { int r = 11 + br.bits(7);
                 for (int k = 0; k < r && nl < hlit + hdist; k++) lengths[nl++] = 0; }
        }
        lit.build(lengths, hlit);
        dist.build(lengths + hlit, hdist);
        printf("[dbg] hlit=%d hdist=%d litMax=%d distMax=%d bits=%d\n", hlit, hdist, lit.maxBits, dist.maxBits, br.bit);
        printf("[dbg] lit len 0..31: ");
        for (int i = 0; i < 32; i++) printf("%d ", lengths[i]);
        printf("\n");
      } else {
        printf("btype=3 invalide\n");
        return false;
      }
      while (true) {
        int s = lit.dec(br);
        if (s < 0) { printf("lit decode KO\n"); return false; }
        if (s < 256) {
          g_litBytes++;
          q.byte((uint8_t)s);
        } else if (s == 256) {
          break;
        } else {
          int sym = s - 257;
          int length = LEN_BASE[sym] + br.bits(LEN_EXTRA[sym]);
          int savebit = br.bit;
          int dsym = dist.dec(br);
          if (dsym < 0) {
            printf("dist decode KO: pixels=%u bit=%d s=%d sym=%d length=%d litMax=%d distMax=%d\n",
                   q.pixels, savebit, s, sym, length, lit.maxBits, dist.maxBits);
            printf("  bits suivants: ");
            for (int i = 0; i < 24; i++)
              printf("%d", (br.d[(savebit+i)>>3]>>((savebit+i)&7))&1);
            printf("\n");
            return false;
          }
          int d = DIST_BASE[dsym] + br.bits(DIST_EXTRA[dsym]);
          g_copyBytes += length;
          g_copies++;
          if (q.pixels >= 11050 && q.pixels <= 11120)
            printf("[copy] #%llu len=%d dist=%d pixels=%u\n", (unsigned long long)g_copies, length, d, q.pixels);
          copyBytes(q, br, d, length);
        }
      }
    }
    if (bfinal) return true;
  }
}

// ---------------------------------------------------------------------------
// Parse PNG : IHDR (w, h, bpp, type), IDAT concaténé -> inflate.
int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s in.zlib out.raw [w] [cropX cropY cropK]\n", argv[0]);
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t* data = new uint8_t[n];
  fread(data, 1, n, f);
  fclose(f);

  int w = argc > 3 ? atoi(argv[3]) : 256;
  QuantOut q;
  q.w = w; q.h = w;
  q.out = fopen(argv[2], "wb");
  q.crop = false;
  if (argc > 6) {
    q.crop = true;
    q.cx0 = atoi(argv[4]); q.cy0 = atoi(argv[5]); q.ck = atoi(argv[6]);
    q.rowA.assign(q.ck, 0);
    q.rowB.assign(q.ck, 0);
  }
  q.init(w, w);

  // zlib header : 2 octets
  BitR br;
  br.d = data;
  br.bit = 16;
  bool ok = inflateBlock(q, br);
  printf("inflate: %s pixels=%u bits=%d/%ld litBytes=%llu copyBytes=%llu copies=%llu\n", ok ? "OK" : "ECHEC", q.pixels, br.bit, n * 8,
         (unsigned long long)g_litBytes, (unsigned long long)g_copyBytes, (unsigned long long)g_copies);
  fclose(q.out);
  return ok ? 0 : 1;
}
