# Fetched data (`data` block)

Use only when the clock shows live values. The TV polls each URL itself.

```json
"data": [
  {"id": "weather", "url": "https://example.com/now.json", "interval": 900, "insecureTls": true,
   "fields": [{"id": "temp", "path": "current.temperature_2m"}, {"id": "code", "path": "current.weather_code"}]}
]
```

## Rules (firmware-enforced)

- At most 4 sources, 1–8 fields each. IDs: letters, digits, `-`, `_` (source ≤ 32, field ≤ 24).
  In spec 2 a source cannot be called `time` or `device`.
- `url` ≤ 200 bytes, `http://` or `https://`. HTTPS **requires** `"insecureTls": true`
  (the TV cannot verify certificates). HTTPS is skipped while free heap is < 18,000 bytes,
  which happens on the ESP8266 boards; prefer `http://` for LAN endpoints.
- Plain GET only: no headers, no auth tokens, no POST. Response ≤ 2 KiB. One request at a
  time, 3 s deadline. `interval` 10–86400 s.
- `path` is a dotted path through **objects** only (`main.temp`, `current.weather_code`).
  Arrays cannot be indexed, so most "forecast" arrays are unusable.
- Values become strings: strings as-is but cut to 31 characters; numbers and booleans as
  their JSON text (`18`, `7.9`, `true`); missing/null/objects → `--`. Only printable ASCII
  renders properly (no `°`).
- A field may set `"decimals"` (0–3, spec 2 only, next release after firmware v3.6.0): text shows its
  number rounded to that many places, half away from zero (`13.5` → `14`, `7` with 1 → `7.0`,
  never `-0`). Bindings still get the exact value. Words and exponent forms (`1e-6`) show as fetched.
- Text uses them as `{weather.temp}`; bindings use the same name as `source`. A missing or
  non-numeric value keeps the layer's static value; a failed fetch keeps the last value.
- Installing the theme makes the TV poll these URLs, LAN addresses included. Tell the user.

Preview with injected values (no network): every declared field you want to see, once:

```
python3 stheme.py preview my-clock out/my-clock --data weather.temp=12.4 --data weather.code=61
```

## Recipe: weather from Open-Meteo (no key)

Open-Meteo's forecast API with only `current=` parameters returns a few hundred bytes, well
under 2 KiB. Ask the user for their place and look up latitude/longitude (two decimals are
enough); keep the URL short.

```json
{"id": "wx",
 "url": "https://api.open-meteo.com/v1/forecast?latitude=52.37&longitude=4.89&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day",
 "interval": 900, "insecureTls": true,
 "fields": [
   {"id": "temp", "path": "current.temperature_2m"},
   {"id": "hum", "path": "current.relative_humidity_2m"},
   {"id": "code", "path": "current.weather_code"},
   {"id": "wind", "path": "current.wind_speed_10m"},
   {"id": "day", "path": "current.is_day"}]}
```

- Units: add `&temperature_unit=fahrenheit` or `&wind_speed_unit=mph` when asked.
- Text: `"{wx.temp}C"` (ASCII "C"; draw a tiny circle r=2 stroke for a degree mark if wanted).
- `weather_code` is a WMO number (0 clear, 1–3 partly cloudy → overcast, 45/48 fog,
  51–57 drizzle, 61–67 rain, 71–77 snow, 80–82 showers, 85/86 snow showers, 95–99
  thunderstorm). There are no conditions or lookups, so a code cannot become a word; show it
  through **color stops** instead, e.g. on an icon-like circle or a condition bar:

```json
"bind": {"fill": {"source": "wx.code", "stops": [
  {"at": 0, "value": "#f0b429"}, {"at": 1, "value": "#9bacc1"}, {"at": 45, "value": "#7f93ab"},
  {"at": 51, "value": "#47c2ff"}, {"at": 71, "value": "#e9edf4"}, {"at": 80, "value": "#47c2ff"},
  {"at": 85, "value": "#e9edf4"}, {"at": 95, "value": "#e5484d"}]}}
```

- Temperature-colored text: color stops on `wx.temp`, e.g. `<0` `#47c2ff`, `0` `#e9edf4`,
  `25` `#f0b429`, `30` `#e5484d`.
- A thermometer bar: rectangle `width` bound to `wx.temp` with `input [-10, 35]`,
  `output [0, 200]`, static width 1.
- `interval` 900 (15 min) matches Open-Meteo's update rate; do not go below 300.
- Because Open-Meteo is HTTPS, it will not refresh on ESP8266 boards when heap is low; on
  those, suggest a small LAN proxy (Home Assistant, Node-RED, n8n) serving the same JSON over
  `http://`.

## Generic JSON endpoint

Ask the user for the URL and a sample response; choose field paths from the sample; check
the response is under 2 KiB and needs no auth. Home Assistant's REST API needs a token, so
it cannot be called directly: the user needs a small endpoint (an HA webhook/automation, a
Node-RED or n8n flow) that returns flat JSON like `{"label": "...", "level": 82}`.
`examples/themes/live-status` in smalltv-mod is a complete dashboard using such an endpoint.
