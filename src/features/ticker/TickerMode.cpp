#include "TickerMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "Net.h"
#include "StockClient.h"

TickerMode g_tickerMode;

// ---- sparkline ------------------------------------------------------------
// livePt (NAN = none) is appended as the newest point so the chart ends at the
// live price rather than the last historical close.
static void drawSparkline(Arduino_GFX* gfx, const StockData& d,
                          int top, int bottom, uint16_t color, float livePt) {
  if (d.sparkCount < 2) return;
  bool live = !isnan(livePt);
  uint8_t n = d.sparkCount + (live ? 1 : 0);
  float mn = d.spark[0], mx = d.spark[0];
  for (uint8_t i = 1; i < d.sparkCount; i++) {
    if (d.spark[i] < mn) mn = d.spark[i];
    if (d.spark[i] > mx) mx = d.spark[i];
  }
  if (live) {
    if (livePt < mn) mn = livePt;
    if (livePt > mx) mx = livePt;
  }
  float span = mx - mn;
  if (span <= 0) span = 1;

  const int padL = 6, padR = 6;
  int plotW = TFT_WIDTH - padL - padR;
  int plotH = bottom - top;

  int prevX = 0, prevY = 0;
  for (uint8_t i = 0; i < n; i++) {
    float v = (i < d.sparkCount) ? d.spark[i] : livePt;
    int x = padL + (int)((long)plotW * i / (n - 1));
    int y = bottom - (int)((v - mn) / span * plotH);
    if (i > 0) {
      gfx->drawLine(prevX, prevY, x, y, color);
      gfx->drawLine(prevX, prevY + 1, x, y + 1, color); // 2px thick
    }
    prevX = x;
    prevY = y;
  }
}

// ---- number formatting ----------------------------------------------------
static void fmtPrice(float v, char* out, size_t n) {
  float a = fabsf(v);
  if (a >= 1000)      snprintf(out, n, "%.2f", v);
  else if (a >= 1)    snprintf(out, n, "%.2f", v);
  else if (a >= 0.01) snprintf(out, n, "%.4f", v);
  else                snprintf(out, n, "%.6f", v);
}

// ---- one ticker page ------------------------------------------------------
static void drawStock(const StockData& d, uint8_t pageIndex, uint8_t pageCount,
                      const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  // No data yet for this symbol.
  if (!d.valid) {
    gfxDrawCentered(d.symbol[0] ? d.symbol : "----", 80, 3, C_WHITE);
    if (d.error && d.lowHeap) {
      gfxDrawCentered("not enough memory", 120, 2, C_GRAY);
      gfxDrawCentered("for HTTPS", 142, 2, C_GRAY);
    } else {
      gfxDrawCentered(d.error ? "fetch error" : "loading...", 120, 2, C_GRAY);
    }
    if (s.ticker.showPageDots) {
      int total = pageCount * 10 - 4;
      int x0 = (TFT_WIDTH - total) / 2;
      for (uint8_t i = 0; i < pageCount; i++)
        gfx->fillCircle(x0 + i * 10 + 2, 230, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    }
    return;
  }

  // Trend color — from the displayed change (chart-range or 1-day basis)
  float chg = 0, pct = 0;
  bool onRange = false;
  bool hasChange = stockDisplayChange(d, s.ticker, chg, pct, &onRange);
  bool up = hasChange ? (chg >= 0) : true;
  uint16_t upC   = s.ticker.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = s.ticker.colorInverted ? C_GREEN : C_RED;
  uint16_t trendC = !hasChange ? C_WHITE : (up ? upC : downC);

  int y = 6;

  // Page dots (top) — a single ticker still gets its one dot
  if (s.ticker.showPageDots) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 3, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 20;                       // extra breathing room below the dots
  }

  // Name / symbol
  if (s.ticker.showName) {
    const char* label = d.name[0] ? d.name : d.symbol;
    gfxDrawCentered(label, y, gfxFitSize(label, 232, 3), C_WHITE);
    y += 28;
  }

  // Price (big, auto-fit)
  if (s.ticker.showPrice) {
    char num[20];
    fmtPrice(d.price, num, sizeof(num));
    char line[28];
    snprintf(line, sizeof(line), "%s%s", d.currency, num);
    uint8_t sz = gfxFitSize(line, 236, 6);
    int ph = 8 * sz;
    int py = s.ticker.showName ? 74 : 64;
    gfxDrawCentered(line, py, sz, C_WHITE);   // price stays neutral (not trend-colored)
    y = py + ph + 8;
  }

  // Change line: [arrow] +chg (+pct%)
  if (s.ticker.showChange && hasChange) {
    char line[40];
    if (pct != 0 || chg != 0)
      snprintf(line, sizeof(line), "%+.2f (%+.2f%%)", chg, pct);
    else
      snprintf(line, sizeof(line), "%+.2f", chg);
    uint8_t sz = gfxFitSize(line, 210, 2);
    int tw = gfxTextW(line, sz);
    int ah = 8 * sz;             // arrow box height
    int aw = ah;
    int totalW = aw + 4 + tw;
    int x = (TFT_WIDTH - totalW) / 2;
    if (x < 2) x = 2;
    // arrow triangle
    int ax = x, ay = y;
    if (up)
      gfx->fillTriangle(ax, ay + ah, ax + aw, ay + ah, ax + aw / 2, ay, trendC);
    else
      gfx->fillTriangle(ax, ay, ax + aw, ay, ax + aw / 2, ay + ah, trendC);
    gfx->setTextSize(sz);
    gfx->setTextColor(trendC);
    gfx->setCursor(x + aw + 4, ay);
    gfx->print(line);
    y = ay + ah + 8;
  }

  // Position P/L vs the cost basis (symbols with qty and cost configured)
  if (s.ticker.showPortfolio && d.qty > 0 && d.cost > 0) {
    float plPct = (d.price / d.cost - 1.0f) * 100.0f;
    char pl[40];
    snprintf(pl, sizeof(pl), "P/L %+.0f (%+.1f%%)", (d.price - d.cost) * d.qty, plPct);
    uint8_t sz = gfxFitSize(pl, 220, 2);
    gfxDrawCentered(pl, y, sz, plPct >= 0 ? upC : downC);
    y += 8 * sz + 6;
  }

  // Chart (with the live price as its newest point when the displayed change
  // is actually on the range basis, so the drawn end matches the number)
  if (s.ticker.showChart && d.sparkCount >= 2) {
    int top = y < 150 ? 156 : y + 4;
    int bottom = 228;
    float livePt = onRange ? d.price : NAN;
    if (top < bottom - 10) drawSparkline(gfx, d, top, bottom, trendC, livePt);
  }

  // Range label (top-right; the very bottom row is overscanned on this panel)
  if (s.ticker.showRangeLabel && d.rangeLabel[0]) {
    int sz = 2;
    int tw = gfxTextW(d.rangeLabel, sz);
    gfx->setTextSize(sz);
    gfx->setTextColor(C_GRAY);
    gfx->setCursor(TFT_WIDTH - tw - 4, 4);
    gfx->print(d.rangeLabel);
  }

  // Updated-ago (bottom-left)
  if (s.ticker.showUpdatedAgo && d.lastOkMs) {
    uint32_t ago = (millis() - d.lastOkMs) / 1000;
    char buf[12];
    if (ago < 100) snprintf(buf, sizeof(buf), "%lus", (unsigned long)ago);
    else           snprintf(buf, sizeof(buf), "%lum", (unsigned long)(ago / 60));
    gfx->setTextSize(2);
    gfx->setTextColor(d.error ? C_RED : C_DGRAY);
    gfx->setCursor(4, 224);
    gfx->print(buf);
  }

  // Stale/error dot (top-left) when last refresh failed but we have old data.
  // (Top-right now holds the range label.)
  if (d.error) gfx->fillCircle(6, 6, 3, C_RED);
}

// ---- portfolio page --------------------------------------------------------
// Compact value: the 240 px row has ~6 characters for it at text size 2.
static void fmtVal(float v, char* out, size_t n) {
  float a = fabsf(v);
  if      (a >= 1000000) snprintf(out, n, "%.2fM", v / 1000000);
  else if (a >= 10000)   snprintf(out, n, "%.1fk", v / 1000);
  else if (a >= 100)     snprintf(out, n, "%.0f", v);
  else                   snprintf(out, n, "%.2f", v);
}

// The summary page exists once any symbol carries a position.
static bool hasPortfolioPage(const Settings& s) {
  if (!s.ticker.showPortfolio) return false;
  for (uint8_t i = 0; i < stocksCount(); i++)
    if (stockAt(i).qty > 0) return true;
  return false;
}

// One row per position (name / P/L% / value), then a total per currency.
static void drawPortfolio(uint8_t pageIndex, uint8_t pageCount, const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  int y = 6;
  if (s.ticker.showPageDots) {
    int total = pageCount * 10 - 4;
    int x0 = (TFT_WIDTH - total) / 2;
    for (uint8_t i = 0; i < pageCount; i++)
      gfx->fillCircle(x0 + i * 10 + 2, y + 3, 2, i == pageIndex ? C_WHITE : C_DGRAY);
    y += 20;
  }

  gfxDrawCentered("Portfolio", y, 3, C_WHITE);
  y += 32;

  uint16_t upC   = s.ticker.colorInverted ? C_RED : C_GREEN;
  uint16_t downC = s.ticker.colorInverted ? C_GREEN : C_RED;

  // Totals bucketed by currency prefix (usually just one bucket).
  float totV[3] = {0, 0, 0}, totC[3] = {0, 0, 0};
  char  totCur[3][6];
  uint8_t curN = 0;

  for (uint8_t i = 0; i < stocksCount(); i++) {
    const StockData& d = stockAt(i);
    if (d.qty <= 0) continue;
    if (y > 186) break;                       // keep room for the totals

    char nm[10];
    strlcpy(nm, d.name[0] ? d.name : d.symbol, sizeof(nm));
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(4, y);
    gfx->print(nm);

    if (d.valid) {
      float val = d.qty * d.price;
      char vbuf[12];
      fmtVal(val, vbuf, sizeof(vbuf));
      gfx->setCursor(TFT_WIDTH - gfxTextW(vbuf, 2) - 4, y);
      gfx->print(vbuf);
      if (d.cost > 0) {
        float plPct = (d.price / d.cost - 1.0f) * 100.0f;
        char pbuf[10];
        snprintf(pbuf, sizeof(pbuf), "%+.1f%%", plPct);
        gfx->setTextColor(plPct >= 0 ? upC : downC);
        gfx->setCursor(116, y);
        gfx->print(pbuf);
      }
      uint8_t b = 0xFF;
      for (uint8_t k = 0; k < curN; k++)
        if (!strcmp(totCur[k], d.currency)) { b = k; break; }
      if (b == 0xFF && curN < 3) {
        b = curN++;
        strlcpy(totCur[b], d.currency, sizeof(totCur[b]));
      }
      if (b != 0xFF) {
        totV[b] += val;
        if (d.cost > 0) totC[b] += d.qty * d.cost;
      }
    } else {
      const char* st = d.error ? (d.lowHeap ? "mem" : "err") : "...";
      gfx->setTextColor(d.error ? C_RED : C_GRAY);
      gfx->setCursor(TFT_WIDTH - gfxTextW(st, 2) - 4, y);
      gfx->print(st);
    }
    y += 18;
  }

  y += 2;
  gfx->drawFastHLine(4, y, TFT_WIDTH - 8, C_DGRAY);
  y += 6;
  for (uint8_t k = 0; k < curN && y <= 214; k++) {
    char vbuf[12];
    fmtVal(totV[k], vbuf, sizeof(vbuf));
    char line[20];
    snprintf(line, sizeof(line), "%s%s", totCur[k], vbuf);
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(4, y);
    gfx->print("Total");
    gfx->setCursor(TFT_WIDTH - gfxTextW(line, 2) - 4, y);
    gfx->print(line);
    if (totC[k] > 0) {
      float plPct = (totV[k] / totC[k] - 1.0f) * 100.0f;
      char pbuf[10];
      snprintf(pbuf, sizeof(pbuf), "%+.1f%%", plPct);
      gfx->setTextColor(plPct >= 0 ? upC : downC);
      gfx->setCursor(78, y);
      gfx->print(pbuf);
    }
    y += 18;
  }
}

// ---- DisplayMode ----------------------------------------------------------
void TickerMode::begin(const Settings& s) {
  stocksInit(s);
  curPage_ = 0;
  lastRotate_ = millis();
  renderedLastOk_ = 0xFFFFFFFF;
  renderedError_ = false;
  needRender_ = true;
}

void TickerMode::invalidate(const Settings& s) {
  stocksInit(s);
  stocksForceRefresh();
  renderedLastOk_ = 0xFFFFFFFF;
  needRender_ = true;
}

void TickerMode::render(const Settings& s) {
  uint8_t n = stocksCount();
  if (n == 0) {
    gfxMessage("No tickers", netIP().c_str(), C_YELLOW);
    return;
  }
  // Only complain about a missing webhook URL if every ticker depends on it.
  bool allWebhook = true;
  for (uint8_t i = 0; i < n; i++)
    if (stockAt(i).source != SRC_WEBHOOK) { allWebhook = false; break; }
  if (allWebhook && s.ticker.webhookUrl.length() < 8) {
    gfxMessage("Set webhook", netIP().c_str(), C_YELLOW);
    return;
  }
  uint8_t pages = n + (hasPortfolioPage(s) ? 1 : 0);
  if (curPage_ >= pages) curPage_ = 0;
  if (curPage_ >= n) drawPortfolio(curPage_, pages, s);
  else               drawStock(stockAt(curPage_), curPage_, pages, s);
}

void TickerMode::service(const Settings& s) {
  stocksService(s);

  uint8_t n = stocksCount();
  uint8_t pages = n + (hasPortfolioPage(s) ? 1 : 0);

  // Rotate to the next page (symbols, then the portfolio summary)
  if (pages > 1 && millis() - lastRotate_ >= (uint32_t)s.ticker.rotateSec * 1000UL) {
    curPage_ = (curPage_ + 1) % pages;
    lastRotate_ = millis();
    needRender_ = true;
  }

  // Re-render when the displayed data changed. The portfolio page watches all
  // symbols at once via a summed fingerprint of their last-update stamps.
  if (n > 0 && curPage_ < n) {
    const StockData& d = stockAt(curPage_);
    if (d.lastOkMs != renderedLastOk_ || d.error != renderedError_) {
      needRender_ = true;
      renderedLastOk_ = d.lastOkMs;
      renderedError_ = d.error;
    }
  } else if (n > 0 && pages > n) {
    uint32_t h = 0;
    bool err = false;
    for (uint8_t i = 0; i < n; i++) {
      h += stockAt(i).lastOkMs;
      err |= stockAt(i).error;
    }
    if (h != renderedLastOk_ || err != renderedError_) {
      needRender_ = true;
      renderedLastOk_ = h;
      renderedError_ = err;
    }
  }

  if (needRender_) {
    render(s);
    needRender_ = false;
  }
}
