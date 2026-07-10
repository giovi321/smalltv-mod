# n8n webhook contract

> **Optional.** Since the firmware can fetch **Yahoo Finance directly** (the
> default data source — see the main [README](../README.md#data-source)), you
> only need this if you want to own the data source: another provider, caching,
> auth, custom symbols, etc. Switch **Display → Data source** to *Custom webhook*
> to use it.

The SmallTV **pulls** data — it periodically calls your webhook once per ticker.
Any backend that speaks the small JSON contract below works (n8n, Node-RED, a
Flask app, a static file…). Two ready-to-import n8n workflows are included:

- [`smalltv-stock-webhook.json`](smalltv-stock-webhook.json) sources everything
  from Yahoo Finance — the same endpoint the firmware calls on its own.
- [`smalltv-stock-webhook-cash.json`](smalltv-stock-webhook-cash.json) routes
  cash.ch listing keys (symbols like `147478611-246-333`) to cash.ch's public
  GraphQL API and everything else to Yahoo, so Yahoo tickers and Swiss
  instruments mix in one rotation. The firmware can also query cash.ch
  directly (see [Data sources](https://giovi321.github.io/smalltv-mod/reference/data-sources/)),
  but the built-in source setting is global — the webhook variant is how you mix.

Import one or the other — both use the webhook path `stock`, so n8n will refuse
to activate them side by side.

## Request (device → your webhook)

```
GET  <webhookUrl>?symbol=<SYMBOL>&range=<RANGE>&points=<N>
```

| query    | meaning                                            | example   |
|----------|----------------------------------------------------|-----------|
| `symbol` | the ticker, exactly as you typed it in the web UI  | `AAPL`, `BTC-USD`, `EURUSD=X` |
| `range`  | the chart timeframe from **Display → Chart timeframe** | `1d`, `5d`, `1mo`, `1y` |
| `points` | max sparkline points the device wants              | `48`      |

`<webhookUrl>` is whatever you set in the web UI (e.g.
`http://n8n.local:5678/webhook/stock`). The device appends the query string,
choosing `?` or `&` automatically.

> The device handles **one symbol per request** and rotates through your list.
> This keeps the firmware simple and your workflow trivial (one symbol in → one
> object out).

## Response (your webhook → device)

`Content-Type: application/json`, a **single JSON object**:

```json
{
  "symbol": "AAPL",
  "name": "Apple Inc.",
  "price": 234.56,
  "currency": "$",
  "change": 2.34,
  "changePct": 1.01,
  "spark": [230.1, 231.0, 229.8, 233.2, 234.56],
  "range": "1D",
  "ok": true
}
```

| field       | type    | required | notes |
|-------------|---------|----------|-------|
| `price`     | number  | **yes**  | current value. If missing/`null`, the device shows an error for this ticker. |
| `name`      | string  | no       | shown instead of the symbol. Defaults to the symbol. Keep it short (≤ ~12 chars look best). |
| `currency`  | string  | no       | prefix shown before the price, e.g. `"$"`, `"€"`, `""`. |
| `change`    | number  | no       | absolute change in price units. |
| `changePct` | number  | no       | percentage change. |
| `spark`     | array   | no       | close prices, **oldest → newest**, max 60 points. Drives the chart. |
| `range`     | string  | no       | label shown bottom-right, e.g. `"1D"`. Defaults to the requested range. |
| `ok`        | boolean | no       | set `false` to explicitly signal "no data" for this symbol. |

Notes:
- You may send **only one** of `change` / `changePct` — the device derives the
  other from `price`.
- `change`/`changePct` colour everything: ≥ 0 → up colour, < 0 → down colour
  (green/red, swappable in the UI).
- Extra fields are ignored, so you can add your own without breaking anything.

## Import the example workflow

1. n8n → **Workflows → Import from File** → pick `smalltv-stock-webhook.json`.
2. Open the **Webhook** node, copy its **Production URL**
   (looks like `https://<your-n8n>/webhook/stock`).
3. **Activate** the workflow.
4. Put that URL in the SmallTV web UI → **Display → Webhook URL**, and add your
   tickers under **Symbols**.

The example maps the timeframe to a candle interval like this:

| `range` | Yahoo interval |
|---------|----------------|
| `1d`    | 5m  |
| `5d`    | 30m |
| `1mo` / `3mo` / `6mo` / `ytd` | 1d |
| `1y` / `2y` | 1wk |
| `5y` / `max` | 1mo |

### HTTPS note for the ESP8266

The ESP8266 can do HTTPS, but TLS is RAM-hungry. If you hit instability, expose
the webhook over plain **HTTP on your LAN** (the device is LAN-only anyway) — set
the webhook URL to `http://…`. The firmware auto-detects `http`/`https` from the
URL scheme; HTTPS is validated *insecurely* (no certificate check), which is fine
for a self-hosted endpoint on your own network.
