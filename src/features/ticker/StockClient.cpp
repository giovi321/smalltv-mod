#include "StockClient.h"
#include "Platform.h"
#include <ArduinoJson.h>
#include <math.h>

static StockData g_stocks[MAX_SYMBOLS];
static uint8_t   g_count = 0;

static bool     g_refreshing = false;
static uint8_t  g_fetchIdx = 0;
static uint32_t g_nextPollMs = 0;

// ---------------------------------------------------------------------------
void stocksInit(const Settings& s) {
  g_count = s.ticker.symbolCount;
  for (uint8_t i = 0; i < g_count; i++) {
    g_stocks[i].clear();
    strlcpy(g_stocks[i].symbol, s.ticker.symbols[i].symbol, MAX_SYMBOL_LEN);
    g_stocks[i].userNamed = (s.ticker.symbols[i].name[0] != 0);
    strlcpy(g_stocks[i].name,
            g_stocks[i].userNamed ? s.ticker.symbols[i].name : s.ticker.symbols[i].symbol,
            MAX_NAME_LEN);
  }
  g_refreshing = false;
  g_nextPollMs = millis();
}

void stocksForceRefresh() {
  g_nextPollMs = millis();
  g_refreshing = false;
}

uint8_t          stocksCount()        { return g_count; }
const StockData& stockAt(uint8_t i)   { return g_stocks[i]; }

bool stocksAnyValid() {
  for (uint8_t i = 0; i < g_count; i++)
    if (g_stocks[i].valid) return true;
  return false;
}

// ---- URL helpers ----------------------------------------------------------
static String urlEncode(const char* src) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (const char* p = src; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

static String buildWebhookUrl(const Settings& s, const char* symbol) {
  String url = s.ticker.webhookUrl;
  char sep = (url.indexOf('?') >= 0) ? '&' : '?';
  url += sep;
  url += "symbol=" + urlEncode(symbol);
  url += "&range=" + urlEncode(s.ticker.range.c_str());
  url += "&points=" + String(s.ticker.points);
  return url;
}

// Map the chart timeframe to a sensible Yahoo candle interval (mirrors the
// reference n8n workflow so the sparkline has a useful number of points).
static const char* yahooInterval(const String& r) {
  if (r == "1d")  return "5m";
  if (r == "5d")  return "30m";
  if (r == "1mo" || r == "3mo" || r == "6mo" || r == "ytd") return "1d";
  if (r == "1y"  || r == "2y")  return "1wk";
  if (r == "5y"  || r == "10y" || r == "max") return "1mo";
  return "1d";
}

static String buildYahooUrl(const Settings& s, const char* host, const char* symbol) {
  String range = s.ticker.range;
  range.toLowerCase();
  if (range.length() == 0) range = "1d";
  String url = F("https://");
  url += host;
  url += F(YAHOO_CHART_PATH);
  url += urlEncode(symbol);          // e.g. AAPL, NESN.SW, BTC-USD, EURUSD=X
  url += F("?range=");
  url += range;
  url += F("&interval=");
  url += yahooInterval(range);
  return url;
}

// Short, display-safe currency prefix. The built-in bitmap font has no glyphs
// for symbols like €, so non-USD currencies are shown as their ISO code.
static void yahooCurrency(const char* code, char* out, size_t n) {
  if (!code || !code[0]) { out[0] = 0; return; }
  if (!strcmp(code, "USD")) { strlcpy(out, "$", n); return; }
  snprintf(out, n, "%s ", code);     // "CHF 79.73", "EUR 1.08", ...
}

// ---- URL builders: cash.ch --------------------------------------------------
// cash.ch answers hand-written GraphQL sent as a plain GET ?query=... — no
// auth, no cookies, no required headers (see config.h). The symbol is the
// cash.ch listing key (valor-marketId-currencyId).

// Daily closes to request per chart timeframe. The server trims to the newest
// N (max=N) inside a fixed catch-all from/to window, so the device needs no
// clock; asking for more than cash.ch stores (~6 months) just returns it all.
static uint16_t cashRangeDays(const String& r) {
  if (r == "1d")  return 2;          // >=2 points so a line can be drawn
  if (r == "5d")  return 5;
  if (r == "1mo") return 22;
  if (r == "3mo") return 65;
  if (r == "6mo" || r == "ytd") return 130;
  if (r == "1y")  return 260;
  return 400;                        // 2y/5y/max: everything available
}

static String buildCashUrl(const String& query) {
  String url = F("https://" CASH_GQL_HOST CASH_GQL_PATH "?query=");
  url += urlEncode(query.c_str());
  return url;
}

static String buildCashQuoteUrl(const char* symbol) {
  String q = F("query{quoteList(listingKeys:\"");
  q += symbol;
  q += F("\"){quoteList{edges{node{...on Instrument{"
         "lval iNetVperprV perfPercentage mCur mShortName}}}}}}");
  return buildCashUrl(q);
}

static String buildCashChartUrl(const Settings& s, const char* symbol) {
  String q = F("query{integration{solid{chart(listingKey:\"");
  q += symbol;
  q += F("\" frequency:\"1d\" from:\"2020-01-01\" to:\"2100-01-01\" max:\"");
  q += String(cashRangeDays(s.ticker.range));
  q += F("\"){timeserie{prices{close}}}}}}");
  return buildCashUrl(q);
}

// ---- parse: custom webhook contract ---------------------------------------
static bool parseWebhook(const Settings& s, StockData& d, Stream& stream) {
  // Filter so unexpected/large fields don't blow up the heap.
  JsonDocument filter;
  filter["symbol"] = true;
  filter["name"] = true;
  filter["price"] = true;
  filter["currency"] = true;
  filter["change"] = true;
  filter["changePct"] = true;
  filter["range"] = true;
  filter["ok"] = true;
  filter["spark"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;
  if (!doc["price"].is<float>() && !doc["price"].is<int>()) return false;

  float price = doc["price"].as<float>();
  if (isnan(price)) return false;
  d.price = price;

  if (!d.userNamed && doc["name"].is<const char*>())
    strlcpy(d.name, doc["name"].as<const char*>(), MAX_NAME_LEN);
  strlcpy(d.currency, doc["currency"] | "", sizeof(d.currency));

  const char* rng = doc["range"] | s.ticker.range.c_str();
  strlcpy(d.rangeLabel, rng, sizeof(d.rangeLabel));

  bool hasChg = doc["change"].is<float>() || doc["change"].is<int>();
  bool hasPct = doc["changePct"].is<float>() || doc["changePct"].is<int>();
  d.hasChange = hasChg || hasPct;
  if (hasChg) d.change = doc["change"].as<float>();
  if (hasPct) d.changePct = doc["changePct"].as<float>();
  if (d.hasChange) {
    if (!hasPct && hasChg) {               // derive % from absolute change
      float prev = price - d.change;
      d.changePct = (prev != 0) ? (d.change / prev * 100.0f) : 0;
    } else if (hasPct && !hasChg) {        // derive absolute change from %
      d.change = price * d.changePct / (100.0f + d.changePct);
    }
  }

  d.sparkCount = 0;
  if (doc["spark"].is<JsonArrayConst>()) {
    for (JsonVariantConst v : doc["spark"].as<JsonArrayConst>()) {
      if (d.sparkCount >= MAX_SPARK_POINTS) break;
      d.spark[d.sparkCount++] = v.as<float>();
    }
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: Yahoo Finance chart payload -----------------------------------
static bool parseYahoo(const Settings& s, StockData& d, Stream& stream) {
  // Keep only the handful of fields we need; the full payload is large and the
  // `meta` object alone has many nested members we don't care about.
  JsonDocument filter;
  JsonObject fmeta = filter["chart"]["result"][0]["meta"].to<JsonObject>();
  fmeta["regularMarketPrice"] = true;
  fmeta["chartPreviousClose"] = true;
  fmeta["previousClose"]      = true;
  fmeta["currency"]           = true;
  fmeta["shortName"]          = true;
  fmeta["longName"]           = true;
  filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonObjectConst res  = doc["chart"]["result"][0];
  JsonObjectConst meta = res["meta"];
  if (meta.isNull()) return false;                 // bad symbol => result null
  if (!meta["regularMarketPrice"].is<float>() &&
      !meta["regularMarketPrice"].is<int>()) return false;

  float price = meta["regularMarketPrice"].as<float>();
  if (isnan(price)) return false;
  d.price = price;

  yahooCurrency(meta["currency"] | "", d.currency, sizeof(d.currency));

  if (!d.userNamed) {
    const char* nm = meta["shortName"] | (meta["longName"] | (const char*)d.symbol);
    if (nm && nm[0]) strlcpy(d.name, nm, MAX_NAME_LEN);
  }

  // Change vs the previous close.
  float prev = NAN;
  if (meta["chartPreviousClose"].is<float>() || meta["chartPreviousClose"].is<int>())
    prev = meta["chartPreviousClose"].as<float>();
  else if (meta["previousClose"].is<float>() || meta["previousClose"].is<int>())
    prev = meta["previousClose"].as<float>();

  if (!isnan(prev) && prev != 0) {
    d.change = price - prev;
    d.changePct = d.change / prev * 100.0f;
    d.hasChange = true;
  } else {
    d.hasChange = false;
  }

  String rl = s.ticker.range;
  rl.toUpperCase();
  strlcpy(d.rangeLabel, rl.c_str(), sizeof(d.rangeLabel));

  // Sparkline from indicators.quote[0].close (oldest -> newest; may hold nulls).
  // Downsample on the fly to at most `points` evenly-spaced samples.
  d.sparkCount = 0;
  JsonArrayConst closes = res["indicators"]["quote"][0]["close"];
  uint16_t want = s.ticker.points;
  if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
  if (!closes.isNull() && want >= 2) {
    uint16_t valid = 0;
    for (JsonVariantConst v : closes) if (!v.isNull()) valid++;
    if (valid >= 2) {
      if (valid <= want) {
        for (JsonVariantConst v : closes) {
          if (v.isNull()) continue;
          if (d.sparkCount >= MAX_SPARK_POINTS) break;
          d.spark[d.sparkCount++] = v.as<float>();
        }
      } else {
        uint16_t i = 0, k = 0;
        float last = 0;
        for (JsonVariantConst v : closes) {
          if (v.isNull()) continue;
          last = v.as<float>();
          // k-th output maps to valid-index round(k*(valid-1)/(want-1)).
          uint16_t target =
              (uint16_t)(((uint32_t)k * (valid - 1) + (want - 1) / 2) / (want - 1));
          if (i == target && k < want) { d.spark[d.sparkCount++] = last; k++; }
          i++;
        }
        if (d.sparkCount > 0) d.spark[d.sparkCount - 1] = last;  // pin newest
      }
    }
  }

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: cash.ch quote ---------------------------------------------------
// {"data":{"quoteList":{"quoteList":{"edges":[{"node":{"lval":"1086.51",
//  "iNetVperprV":"7.36","perfPercentage":"0.68","mCur":"USD",...}}]}}}}
// Numeric fields arrive as JSON *strings*, so read them as text and atof().
static bool parseCashQuote(const Settings& s, StockData& d, Stream& stream) {
  JsonDocument filter;
  JsonObject node =
      filter["data"]["quoteList"]["quoteList"]["edges"][0]["node"].to<JsonObject>();
  node["lval"]           = true;   // last value (price)
  node["iNetVperprV"]    = true;   // absolute day change
  node["perfPercentage"] = true;   // day change in %
  node["mCur"]           = true;
  node["mShortName"]     = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonObjectConst n = doc["data"]["quoteList"]["quoteList"]["edges"][0]["node"];
  const char* lval = n["lval"] | "";       // empty => unknown key / no fix yet
  if (!lval[0]) return false;
  float price = atof(lval);
  if (isnan(price) || price <= 0) return false;
  d.price = price;

  yahooCurrency(n["mCur"] | "", d.currency, sizeof(d.currency));

  if (!d.userNamed) {
    const char* nm = n["mShortName"] | (const char*)d.symbol;
    if (nm && nm[0]) strlcpy(d.name, nm, MAX_NAME_LEN);
  }

  const char* chg = n["iNetVperprV"]    | "";
  const char* pct = n["perfPercentage"] | "";
  d.hasChange = chg[0] || pct[0];
  if (d.hasChange) {
    d.change    = atof(chg);
    d.changePct = pct[0] ? atof(pct) : 0;
    if (!pct[0]) {                          // derive % from the absolute change
      float prev = price - d.change;
      d.changePct = (prev != 0) ? (d.change / prev * 100.0f) : 0;
    }
  }

  String rl = s.ticker.range;
  rl.toUpperCase();
  strlcpy(d.rangeLabel, rl.c_str(), sizeof(d.rangeLabel));

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

// ---- parse: cash.ch daily-close series --------------------------------------
// {"data":{"integration":{"solid":{"chart":{"timeserie":{"prices":
//  [{"close":998.45},...]}}}}}} — closes are real JSON numbers here (unlike
// the quote), oldest -> newest. Downsampling mirrors the Yahoo parser.
static bool parseCashChart(const Settings& s, StockData& d, Stream& stream) {
  JsonDocument filter;
  filter["data"]["integration"]["solid"]["chart"]["timeserie"]["prices"][0]["close"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, stream, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonArrayConst prices =
      doc["data"]["integration"]["solid"]["chart"]["timeserie"]["prices"];
  if (prices.isNull()) return false;         // unknown key / empty series

  uint16_t want = s.ticker.points;
  if (want > MAX_SPARK_POINTS) want = MAX_SPARK_POINTS;
  if (want < 2) return true;                 // chart not wanted; keep quote data

  uint16_t valid = 0;
  for (JsonObjectConst p : prices)
    if (p["close"].is<float>() || p["close"].is<int>()) valid++;
  if (valid < 2) return false;               // keep the previous sparkline

  d.sparkCount = 0;
  if (valid <= want) {
    for (JsonObjectConst p : prices) {
      if (!p["close"].is<float>() && !p["close"].is<int>()) continue;
      if (d.sparkCount >= MAX_SPARK_POINTS) break;
      d.spark[d.sparkCount++] = p["close"].as<float>();
    }
  } else {
    uint16_t i = 0, k = 0;
    float last = 0;
    for (JsonObjectConst p : prices) {
      if (!p["close"].is<float>() && !p["close"].is<int>()) continue;
      last = p["close"].as<float>();
      // k-th output maps to valid-index round(k*(valid-1)/(want-1)).
      uint16_t target =
          (uint16_t)(((uint32_t)k * (valid - 1) + (want - 1) / 2) / (want - 1));
      if (i == target && k < want) { d.spark[d.sparkCount++] = last; k++; }
      i++;
    }
    if (d.sparkCount > 0) d.spark[d.sparkCount - 1] = last;  // pin newest
  }
  return true;
}

// ---- one HTTP(S) GET + parse ----------------------------------------------
enum ParseKind : uint8_t { PARSE_WEBHOOK, PARSE_YAHOO, PARSE_CASH_QUOTE, PARSE_CASH_CHART };

static bool fetchUrl(const Settings& s, const String& url, ParseKind kind, StockData& d) {
  bool https = url.startsWith("https://");

  std::unique_ptr<NetClient> client;
  if (https) {
    // TLS needs a big contiguous chunk of heap. If we're low, skip this fetch and
    // flag an error instead of letting the TLS allocation fail and reset-loop.
    if (ESP.getFreeHeap() < 16000) return false;
    // Yahoo sends <=~1.3 KB TLS records and cash.ch negotiates 2 KB MFLN
    // fragments, so a 2 KB receive buffer frees heap on the ESP8266.
    client.reset(platformMakeSecureClient(2048));
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  if (kind == PARSE_YAHOO) {
    http.setUserAgent(F(YAHOO_USER_AGENT));   // empty UA => HTTP 429 from Yahoo
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  } else if (kind != PARSE_WEBHOOK) {
    http.setUserAgent(F(CASH_USER_AGENT));    // cash.ch requires none; sent to be identifiable
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  bool ok;
  switch (kind) {
    case PARSE_YAHOO:      ok = parseYahoo(s, d, http.getStream());     break;
    case PARSE_CASH_QUOTE: ok = parseCashQuote(s, d, http.getStream()); break;
    case PARSE_CASH_CHART: ok = parseCashChart(s, d, http.getStream()); break;
    default:               ok = parseWebhook(s, d, http.getStream());   break;
  }
  http.end();
  return ok;
}

// ---- fetch one symbol -----------------------------------------------------
static bool fetchSymbol(const Settings& s, StockData& d) {
  if (s.ticker.source == SRC_YAHOO) {
    // A single back-to-back HTTPS fetch occasionally drops on the ESP8266, so
    // retry once on the alternate mirror before giving up (this is what made one
    // symbol intermittently fail while others in the same poll succeeded).
    if (fetchUrl(s, buildYahooUrl(s, YAHOO_CHART_HOST1, d.symbol), PARSE_YAHOO, d)) return true;
    delay(150);
    return fetchUrl(s, buildYahooUrl(s, YAHOO_CHART_HOST2, d.symbol), PARSE_YAHOO, d);
  }

  if (s.ticker.source == SRC_CASH) {
    // Quote first (price + day change, ~200 B); retry once for the same
    // transient-TLS reason as Yahoo. The sparkline comes from a second slim
    // request whose failure is non-fatal — a stale chart beats no data.
    if (!fetchUrl(s, buildCashQuoteUrl(d.symbol), PARSE_CASH_QUOTE, d)) {
      delay(150);
      if (!fetchUrl(s, buildCashQuoteUrl(d.symbol), PARSE_CASH_QUOTE, d)) return false;
    }
    if (s.ticker.showChart && s.ticker.points >= 2) {
      delay(150);
      fetchUrl(s, buildCashChartUrl(s, d.symbol), PARSE_CASH_CHART, d);
    }
    return true;
  }

  if (s.ticker.webhookUrl.length() < 8) return false;
  return fetchUrl(s, buildWebhookUrl(s, d.symbol), PARSE_WEBHOOK, d);
}

// ---------------------------------------------------------------------------
void stocksService(const Settings& s) {
  if (g_count == 0) return;

  if (!g_refreshing) {
    if ((int32_t)(millis() - g_nextPollMs) >= 0) {
      g_refreshing = true;
      g_fetchIdx = 0;
    } else {
      return;
    }
  }

  // One blocking fetch per call so net/web/display keep getting serviced.
  if (g_fetchIdx < g_count) {
    StockData& d = g_stocks[g_fetchIdx];
    if (!fetchSymbol(s, d)) d.error = true;   // keep stale data, flag error
    g_fetchIdx++;
  }

  if (g_fetchIdx >= g_count) {
    g_refreshing = false;
    uint32_t period = (uint32_t)s.ticker.pollSec * 1000UL;
    g_nextPollMs = millis() + period;
  }
}
