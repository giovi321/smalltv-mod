---
title: Data sources
description: "Where the ticker gets its prices: Yahoo Finance directly, or your own webhook."
---

The ticker can pull prices two ways. Yahoo Finance works out of the box with no server. A custom webhook lets you own the source. Pick one in **Display → Data source**.

## Yahoo Finance, the default

The device fetches Yahoo's public chart endpoint directly over HTTPS, one request per symbol:

```
GET https://query1.finance.yahoo.com/v8/finance/chart/AAPL?range=1d&interval=5m
```

It parses the price, the previous close (for the change and percent change), the currency, and the sparkline itself. Any Yahoo symbol works: US and global stocks and ETFs, Swiss and European stocks (the `.SW` and `.DE` suffixes), crypto (`BTC-USD`), and FX (`EURUSD=X`).

The chart timeframe (`1d`, `5d`, `1mo`, `3mo`, `6mo`, `ytd`, `1y`, `2y`, `5y`, `max`) picks the candle interval automatically.

No API key, no account, no backend. The only requirement is outbound HTTPS, which the device handles with a bounded TLS buffer since Yahoo's records are small. Yahoo's endpoint is unofficial and rate-limited, so keep the refresh interval reasonable. The default 120 seconds is fine for 8 symbols.

## Custom webhook

To own the data (other providers, caching, secrets), switch the source to **Custom webhook**. The device calls your URL, one request per symbol:

```
GET <webhookUrl>?symbol=AAPL&range=1d&points=48
```

and expects a small JSON object back:

```json
{
  "symbol": "AAPL",
  "name": "Apple",
  "price": 234.56,
  "currency": "$",
  "change": 2.34,
  "changePct": 1.01,
  "spark": [230.1, 231.0, 229.8, 234.56],
  "range": "1D",
  "ok": true
}
```

Only `price` is required. The full field table and a ready-to-import n8n workflow are in the repo under [`n8n/`](https://github.com/giovi321/smalltv-mod/tree/main/n8n).

The device pulls rather than receives a push, so your backend never needs to know the device's IP, and it keeps working if that IP changes.

### Why no cash.ch integration

cash.ch exposes only a GraphQL API behind Akamai bot protection, and direct requests return `403`, which an ESP8266 cannot realistically pass. Swiss equities are already covered through Yahoo's `.SW` symbols, priced in CHF, so a dedicated cash.ch path would add fragility for no gain. Use the custom-webhook mode if you ever need a bespoke Swiss source.

## TLS on the ESP8266

HTTPS is RAM-tight on the ESP8266. It works, but for a webhook on your own LAN, plain HTTP is more reliable if you see instability. The ESP32-C2 has more headroom and runs HTTPS without the tuning the ESP8266 needs.
