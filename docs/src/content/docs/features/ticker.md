---
title: Stock and crypto ticker
description: Show live prices, change, and a sparkline for up to 8 rotating symbols.
---

The ticker is the default mode. It shows one symbol at a time and rotates through your list on a timer. For each symbol it draws the price, the absolute change, the percent change with an up or down arrow, and a small sparkline chart.

## What it shows

- The current price, in the symbol's currency.
- Absolute change and percent change since the previous close, coloured green for up and red for down.
- A sparkline over the selected timeframe.
- Optional extras you toggle in the Display tab: the name, the timeframe label, an "updated N s ago" line, and rotation dots.

Non-USD currencies show as their ISO code, for example `CHF 79.73`, because the built-in bitmap font has no glyph for symbols like the euro sign.

## Symbols

Add up to 8 tickers in the **Symbols** tab. The `symbol` field is the Yahoo ticker; the `name` field is an optional label that overrides the source's own name. Examples that work:

| What | Examples |
|------|----------|
| US and global stocks and ETFs | `AAPL`, `MSFT`, `VOO` |
| Swiss and European stocks | `NESN.SW`, `ROG.SW`, `UBSG.SW`, `BMW.DE` |
| Crypto | `BTC-USD`, `ETH-EUR` |
| FX | `EURUSD=X`, `EURCHF=X` |

## Timing and data

Two intervals control the display: how often each symbol is shown (rotation) and how often data is refreshed (poll). Both are set in the Display tab. The default poll of 120 seconds is fine for 8 symbols.

Where the prices come from is a separate choice. By default the device fetches Yahoo Finance directly over HTTPS with no backend. You can also point it at your own webhook. Both are covered in [Data sources](/smalltv-mod/reference/data-sources/).
