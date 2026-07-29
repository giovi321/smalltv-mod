---
title: Data sources
description: "Where each ticker gets its prices: Yahoo Finance, cash.ch, GitHub, or your own webhook."
---

The ticker can pull prices four ways. Yahoo Finance works out of the box with no server. cash.ch covers Swiss instruments Yahoo does not carry, also with no server. GitHub is a serverless proxy for cash.ch that needs nothing of yours running. A custom webhook lets you own the source. Every ticker picks its own source in the **Ticker** tab, so a rotation can mix all of them freely.

## Yahoo Finance, the default

The device fetches Yahoo's public chart endpoint directly over HTTPS, one request per symbol:

```
GET https://query1.finance.yahoo.com/v8/finance/chart/AAPL?range=1d&interval=5m
```

It parses the price, the previous close (for the change and percent change), the currency, and the sparkline itself. Any Yahoo symbol works: US and global stocks and ETFs, Swiss and European stocks (the `.SW` and `.DE` suffixes), crypto (`BTC-USD`), and FX (`EURUSD=X`).

The chart timeframe (`1d`, `5d`, `1mo`, `3mo`, `6mo`, `ytd`, `1y`, `2y`, `5y`, `max`) picks the candle interval automatically.

No API key, no account, no backend. The only requirement is outbound HTTPS, which the device handles with a bounded TLS buffer since Yahoo's records are small. Yahoo's endpoint is unofficial and rate-limited, so keep the refresh interval reasonable. The default 120 seconds is fine for 8 symbols.

## cash.ch, for Swiss instruments

Yahoo does not know many Swiss-listed products: structured products, AMCs and tracker certificates, and anything quoted off-exchange. The Swiss finance portal [cash.ch](https://www.cash.ch) does. The device queries cash.ch's public GraphQL endpoint directly over HTTPS, two small requests per symbol: a ~200-byte quote (price and day change) and a slim series of daily closes for the sparkline. No API key, no account.

With this source the `symbol` field is not a ticker but a cash.ch **listing key** in the form `valor-marketId-currencyId`, for example `147478611-246-333`. The web UI's **cash.ch symbol finder** (Ticker tab) turns a pasted cash.ch link, ISIN, valor, or instrument name into one: it queries cash.ch's search from your browser and one click adds the result as a ticker. Manual fallback: open the instrument's page on cash.ch, open the browser dev tools' Network tab filtered to `graphql`, reload, and read the key from `variables.id` or `listingKeys` of any chart request.

Worth knowing:

- The change fields mean change versus the previous close, same as Yahoo.
- The sparkline uses daily closes; cash.ch keeps roughly the last 6 months, so `6mo` is the longest timeframe with full data. Sub-daily timeframes add nothing for instruments that fix once a day.
- Prices are what cash.ch shows: delayed or fixing prices depending on the venue.
- The endpoint is unofficial and unauthenticated; it can change or disappear without notice. For instruments that fix once daily, a poll of a few minutes is already far more than enough.

### cash.ch on the ESP8266

cash.ch's CDN requires a modern TLS handshake (ECDHE). The ESP32 boards do this without effort. The ESP8266 can do it too, but its memory is tight for the handshake, so the firmware shapes the cash.ch path to fit: only cash.ch is offered ECDHE (Yahoo and the GitHub source stay on the older, cheaper handshake), the connection uses small buffers and resumes the TLS session so only the first fetch per session is expensive, and a fetch is skipped when the heap is momentarily too fragmented and retried a few seconds later. In practice all of this is invisible: the certificates show and update normally.

If you want a device that never even attempts the handshake, the **GitHub** source below is a drop-in alternative for the same listing keys.

One more thing to watch on the ESP8266: the web UI's Display tab has a "Clock & night mode" card, and enabling it runs NTP to know when to dim. NTP only starts while night mode is enabled, but on an ESP8266 that already has cash.ch tickers, the heap NTP holds can be enough to starve the large contiguous block the cash.ch TLS handshake needs, and cash.ch tickers can stop fetching. If you run night mode on an ESP8266 with cash.ch tickers, switch those tickers to the **GitHub** source for the same listing keys, or leave night mode off on that device. ESP32 boards have enough headroom for both at once.

## Custom webhook

To own the data (other providers, caching, secrets), set a ticker's source to **Webhook**. The device calls your URL, one request per symbol:

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

Only `price` is required. The full field table and two ready-to-import n8n workflows (Yahoo-only, and Yahoo + cash.ch in one) are in the repo under [`n8n/`](https://github.com/mosvov/smalltv-mod/tree/main/n8n).

The device pulls rather than receives a push, so your backend never needs to know the device's IP, and it keeps working if that IP changes. Since each ticker picks its own source, webhook tickers mix freely with Yahoo and cash.ch ones in the same rotation.

## GitHub

The **GitHub** source is a serverless proxy for cash.ch that needs nothing of yours running. A scheduled GitHub Action in this repo fetches cash.ch and publishes one small JSON file per listing key to the `data` branch; the device reads it from `raw.githubusercontent.com`, which every board can reach over plain static-RSA HTTPS (the same handshake GitHub self-update and Yahoo use). No ECDHE on the device, so it cannot hit the memory limits the direct cash.ch path pushes against.

Use it the same way as the cash.ch source: set a ticker's source to **GitHub** and its symbol to the cash.ch listing key. The one difference is that a key only works once it is published, so add it to [`quotes-config.json`](https://github.com/mosvov/smalltv-mod/blob/main/quotes-config.json) and let the workflow run (or trigger it once by hand). The workflow is free: GitHub Actions has no minute limit on public repositories.

This is the belt-and-suspenders option. On the ESP32 boards the direct cash.ch source is simpler and needs no published file. On the ESP8266 the direct source works too, but GitHub is there if you would rather the device never do the ECDHE handshake at all.

## TLS on the ESP8266

HTTPS is RAM-tight on the ESP8266. It works, and the firmware tunes each source to fit (see the cash.ch note above), but the ESP32 boards have more headroom and need none of that tuning. For a webhook on your own LAN, plain HTTP sidesteps the TLS cost entirely and is the most reliable option if you see instability.
