"""Writes a .stheme package that the loader accepts but that needs more heap
than a SmallTV-Ultra has free (made-up data, text layers only).

The firmware loads the selected theme again at every boot. A package like this
one can run the device out of memory while it loads or draws. Use it to test
the memory margin after a theme load, and to measure how much free heap the
Wi-Fi code needs: a series of packages with growing size shows where it stops
working.

Usage: python3 tools/make_oom_theme.py <out.stheme> [layers] [chars]
                                       [--data] [--data-url URL] [--interval S]
  layers  text layers, 1..32 (default 32, the most format V1 allows)
  chars   characters in each layer's text, 1..128 (default 65: with 32 layers
          this is the size that crashed a plain 3.8.0 device)
  --data  add one data source with one field, shown by the first layer, so a
          fetch runs while the theme is up (the layer is one of the layers;
          chars must be at least 5 then)
  --data-url   the source's URL (default http://198.51.100.10:8765/d.json, a
          documentation address: give the address of your test server)
  --interval   seconds between fetches, 10..86400 (default 10)

The package is plain data: the STH1 header (file count, 2 reserved bytes), then
per file a 2-byte path length, a 4-byte payload length, the path and the
payload (little endian).
"""
import json
import struct
import sys

MAX_LAYERS = 32
DEFAULT_LAYERS = 32
DEFAULT_CHARS = 65
MAX_CHARS = 128          # the manifest's limit for a text value
MIN_INTERVAL = 10        # the manifest's lower limit for a data interval
MAX_INTERVAL = 86400
MAX_URL = 200            # the manifest's limit for a data URL
DEFAULT_DATA_URL = "http://198.51.100.10:8765/d.json"
DATA_ID = "w"
DATA_FIELD = "p"
TOKEN = "{%s.%s}" % (DATA_ID, DATA_FIELD)


def pack(files):
    out = b"STH1" + struct.pack("<HH", len(files), 0)
    for path, payload in files:
        name = path.encode()
        out += struct.pack("<HI", len(name), len(payload)) + name + payload
    return out


def check_data(url, interval, chars):
    if not url.startswith(("http://", "https://")):
        raise ValueError("data URL must start with http:// or https://")
    if len(url.encode()) > MAX_URL:
        raise ValueError("data URL is longer than %d bytes" % MAX_URL)
    if not MIN_INTERVAL <= interval <= MAX_INTERVAL:
        raise ValueError("interval must be %d..%d" % (MIN_INTERVAL, MAX_INTERVAL))
    if chars < len(TOKEN):
        raise ValueError("chars must be at least %d with a data source" % len(TOKEN))


def manifest(layers, chars, data_url=None, interval=MIN_INTERVAL):
    if not 1 <= layers <= MAX_LAYERS:
        raise ValueError("layers must be 1..%d" % MAX_LAYERS)
    if not 1 <= chars <= MAX_CHARS:
        raise ValueError("chars must be 1..%d" % MAX_CHARS)
    if data_url is not None:
        check_data(data_url, interval, chars)
    texts = []
    for i in range(layers):
        value = ("synthetic %02d " % i + "x" * chars)[:chars]
        if i == 0 and data_url is not None:
            value = (TOKEN + " " + "x" * chars)[:chars]
        texts.append({"id": "t%02d" % i, "type": "text", "x": 8, "y": (i * 7) % 220,
                      "anchor": "top-left", "value": value,
                      "size": 16, "color": "#%06x" % (0x101010 * (i % 15 + 1))})
    m = {"spec": 1,
         "theme": {"id": "oom-test", "name": "Out of memory", "author": "test", "version": "1"},
         "display": {"width": 240, "height": 240, "background": "#000000"}}
    if data_url is not None:
        m["data"] = [{"id": DATA_ID, "url": data_url, "interval": interval,
                      "fields": [{"id": DATA_FIELD, "path": DATA_FIELD}]}]
    m["layers"] = texts
    return m


def build(layers=DEFAULT_LAYERS, chars=DEFAULT_CHARS, data_url=None, interval=MIN_INTERVAL):
    data = json.dumps(manifest(layers, chars, data_url, interval), separators=(",", ":")).encode()
    return pack([("theme.json", data)])


def main(argv):
    args = argv[1:]
    options = {"--data": False, "--data-url": DEFAULT_DATA_URL, "--interval": MIN_INTERVAL}
    positional = []
    try:
        i = 0
        while i < len(args):
            a = args[i]
            if a == "--data":
                options[a] = True
            elif a in ("--data-url", "--interval"):
                i += 1
                options[a] = args[i] if a == "--data-url" else int(args[i])
                options["--data"] = True
            elif a.startswith("--"):
                raise ValueError("unknown option " + a)
            else:
                positional.append(a)
            i += 1
        if not 1 <= len(positional) <= 3:
            raise ValueError("expected <out> [layers] [chars]")
        layers = int(positional[1]) if len(positional) > 1 else DEFAULT_LAYERS
        chars = int(positional[2]) if len(positional) > 2 else DEFAULT_CHARS
        url = options["--data-url"] if options["--data"] else None
        data = build(layers, chars, url, options["--interval"])
    except (ValueError, IndexError) as e:
        sys.stderr.write("make_oom_theme: %s\n\n%s" % (e, __doc__))
        return 2
    with open(positional[0], "wb") as f:
        f.write(data)
    print("%s: %d layers x %d chars%s, %d bytes" % (
        positional[0], layers, chars, ", data source every %d s" % options["--interval"] if url else "", len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
