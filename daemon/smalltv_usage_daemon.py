#!/usr/bin/env python3
"""smalltv usage daemon — serves Claude usage over HTTP for the SmallTV.

Polls the Claude API rate-limit headers (using the OAuth token that Claude Code
already stores on this machine) and serves the latest snapshot as a tiny JSON
object. The SmallTV, in "Claude usage" mode, GETs this endpoint over your LAN —
no cable, no serial port. Point the device's Usage URL at http://<this-pc>:8787/.

By default (and when launched with ``--tray`` via pythonw / install.bat) it shows
a system-tray icon — the little mascot — with live status, Refresh, and Quit.
Pass ``--no-tray`` for headless console mode.

Response body (the contract the firmware parses):
    {"s":29,"sr":142,"w":4,"wr":9876,"st":"allowed","ok":true}
      s  = 5-hour window utilization %     sr = minutes until the 5h window resets
      w  = 7-day window utilization %      wr = minutes until the 7d window resets
      st = rate-limit status               ok = false => "no data" (token missing etc.)

Cross-platform. Core dependency: httpx. Tray also needs: pystray, Pillow.

    pip install -r requirements.txt
    python smalltv_usage_daemon.py            # tray icon + serves on 0.0.0.0:8787
    python smalltv_usage_daemon.py --no-tray  # headless console
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import httpx

# ---- Config ---------------------------------------------------------------

DEFAULT_PORT = 8787
DEFAULT_HOST = "0.0.0.0"
DEFAULT_POLL_INTERVAL = 60   # seconds between Claude API refreshes

CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"

TOKEN_ENDPOINT = "https://platform.claude.com/v1/oauth/token"
TOKEN_REFRESH_MARGIN = 300  # refresh 5 minutes before expiry

API_URL = "https://api.anthropic.com/v1/messages"
API_HEADERS_TEMPLATE = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta": "oauth-2025-04-20",
    "Content-Type": "application/json",
    "User-Agent": "claude-code/2.1.146",
}
API_BODY = {
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 1,
    "messages": [{"role": "user", "content": "hi"}],
}

# The mascot's calm idle pose (claudepix 20x20, frame 0 of "idle breathe"), used
# to draw the tray icon. Digits index MASCOT_PALETTE; 0 is transparent.
MASCOT_ROWS = [
    "00000000000000000000",
    "00000000000000000000",
    "00000000000000000000",
    "00000000000000000000",
    "00000111111111110000",
    "00000111111111110000",
    "00000112111112110000",
    "00011112111112111100",
    "00011111111111111100",
    "00011111111111111100",
    "00010111111111110100",
    "00000111111111110000",
    "00000111111111110000",
    "00000111111111110000",
    "00000100100010010000",
    "00000100100010010000",
    "00000100100010010000",
    "00000000000000000000",
    "00000000000000000000",
    "00000000000000000000",
]
MASCOT_PALETTE = [0x0000, 0xCBED, 0x0861, 0, 0, 0, 0, 0, 0, 0]  # RGB565; 1=body, 2=eye


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


# Run a child process without flashing a console window on Windows (important when
# launched via pythonw — otherwise spawning `claude` pops a visible window).
_NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0  # CREATE_NO_WINDOW


def _run(cmd, **kw):
    if _NO_WINDOW:
        kw["creationflags"] = kw.get("creationflags", 0) | _NO_WINDOW
    return subprocess.run(cmd, **kw)


# Spawning Claude Code is a last-resort token refresh; never do it more than once
# per this many seconds, so a failing direct refresh can't pop it every poll.
_CLAUDE_REFRESH_COOLDOWN = 900
_last_claude_refresh = 0.0


# ---- Shared state ---------------------------------------------------------

class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.status = "Starting..."
        self.payload: dict = {"ok": False}
        self.last_update = 0.0
        self.endpoint = ""
        self.stop_event = threading.Event()
        self.refresh_event = threading.Event()

    def set_status(self, status: str) -> None:
        with self.lock:
            self.status = status

    def set_payload(self, payload: dict, keep_last_good: bool = True) -> None:
        with self.lock:
            if payload.get("ok") or not (keep_last_good and self.payload.get("ok")):
                self.payload = payload
            if payload.get("ok"):
                self.last_update = time.time()

    def get_payload(self) -> dict:
        with self.lock:
            return dict(self.payload)

    def get_tooltip(self) -> str:
        with self.lock:
            lines = [f"smalltv usage — {self.status}"]
            p = self.payload
            if p.get("ok"):
                lines.append(f"5h {p['s']}%   7d {p['w']}%")
            if self.endpoint:
                lines.append(self.endpoint)
            return "\n".join(lines)

    def get_status_key(self) -> str:
        with self.lock:
            s = self.status.lower()
            if "token" in s or "login" in s or "error" in s:
                return "error"
            if self.payload.get("ok"):
                return "ok"
            return "searching"


state = State()


# ---- Credential / token management ----------------------------------------

def _read_credentials_file() -> dict | None:
    try:
        return json.loads(CREDENTIALS_PATH.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def _write_credentials_file(data: dict) -> None:
    try:
        tmp = CREDENTIALS_PATH.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=2))
        tmp.replace(CREDENTIALS_PATH)
    except OSError as e:
        log(f"Error writing credentials: {e}")


def _get_oauth_block(creds: dict) -> dict | None:
    return creds.get("claudeAiOauth") if isinstance(creds, dict) else None


def _extract_access_token(blob: str) -> str | None:
    """Pull accessToken from a credentials blob (macOS Keychain path)."""
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        for v in data.values():
            if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
                return v["accessToken"]
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    if m:
        return m.group(1)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return blob
    return None


def _is_token_expired(oauth: dict) -> bool:
    expires_at = oauth.get("expiresAt")
    if not isinstance(expires_at, (int, float)):
        return False
    return time.time() >= (expires_at / 1000.0 - TOKEN_REFRESH_MARGIN)


def _refresh_token(oauth: dict, creds: dict) -> str | None:
    """Exchange the refresh token for a new access token."""
    refresh_tok = oauth.get("refreshToken")
    if not refresh_tok:
        log("No refresh token available")
        return None
    log("Refreshing OAuth token...")
    try:
        resp = httpx.post(
            TOKEN_ENDPOINT,
            data={"grant_type": "refresh_token", "refresh_token": refresh_tok},
            headers={"User-Agent": API_HEADERS_TEMPLATE["User-Agent"]},
            timeout=20.0,
        )
    except httpx.HTTPError as e:
        log(f"Token refresh request failed: {e}")
        return None
    if resp.status_code >= 400:
        log(f"Token refresh HTTP {resp.status_code}: {resp.text[:200]}")
        return None
    try:
        body = resp.json()
    except (json.JSONDecodeError, ValueError):
        log("Token refresh returned invalid JSON")
        return None
    new_access = body.get("access_token")
    if not new_access:
        log("Token refresh response missing access_token")
        return None
    oauth["accessToken"] = new_access
    if "refresh_token" in body:
        oauth["refreshToken"] = body["refresh_token"]
    if "expires_in" in body:
        oauth["expiresAt"] = int((time.time() + body["expires_in"]) * 1000)
    elif "expires_at" in body:
        oauth["expiresAt"] = int(body["expires_at"] * 1000)
    _write_credentials_file(creds)
    log("Token refreshed successfully")
    return new_access


def _refresh_via_claude_code() -> str | None:
    """Spawn Claude Code (windowless) briefly so it refreshes its own token on disk.

    Rate-limited and a last resort: normally the direct OAuth refresh succeeds and
    this is never reached. The subprocess runs with CREATE_NO_WINDOW so it doesn't
    flash a console window when the daemon runs under pythonw.
    """
    global _last_claude_refresh
    now = time.time()
    if now - _last_claude_refresh < _CLAUDE_REFRESH_COOLDOWN:
        log("Skipping Claude Code refresh (rate-limited)")
        return None
    _last_claude_refresh = now

    log("Spawning Claude Code to refresh token...")
    try:
        _run(["claude", "-p", "hi", "--max-turns", "1"],
             capture_output=True, timeout=30)
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
        log(f"Could not refresh via Claude Code: {e}")
        return None
    creds = _read_credentials_file()
    oauth = _get_oauth_block(creds) if creds else None
    if oauth and not _is_token_expired(oauth):
        log("Token refreshed via Claude Code")
        return oauth.get("accessToken")
    log("Claude Code did not refresh the token - browser login may be required")
    return None


def _read_token_keychain() -> str | None:
    import getpass
    try:
        out = _run(
            ["security", "find-generic-password", "-s",
             "Claude Code-credentials", "-a", getpass.getuser(), "-w"],
            check=True, capture_output=True, text=True, timeout=10,
        )
    except (subprocess.CalledProcessError, FileNotFoundError,
            subprocess.TimeoutExpired) as e:
        log(f"Keychain read failed: {e}")
        return None
    return _extract_access_token(out.stdout)


# Last reason read_token() returned None — surfaced in the tray/console so the
# user sees *why* (e.g. "re-login" vs "not logged in").
_auth_hint = ""


def read_token() -> str | None:
    """Return a Claude OAuth token.

    Prefers CLAUDE_CODE_OAUTH_TOKEN — a long-lived token from `claude setup-token`,
    which is the robust choice for an always-on daemon. Otherwise falls back to the
    credentials Claude Code stores on disk, which expire and (for some subscription
    logins) carry no refresh token, so they can't be renewed headlessly.
    """
    global _auth_hint
    env_tok = os.environ.get("CLAUDE_CODE_OAUTH_TOKEN", "").strip()
    if env_tok:
        _auth_hint = ""
        return env_tok

    if sys.platform == "darwin":
        tok = _read_token_keychain()
        _auth_hint = "" if tok else "Not logged in - run 'claude' to log in"
        return tok

    creds = _read_credentials_file()
    if not creds:
        _auth_hint = "No Claude credentials - run 'claude setup-token' or 'claude'"
        return None
    oauth = _get_oauth_block(creds)
    if not oauth or not isinstance(oauth.get("accessToken"), str):
        _auth_hint = "Not logged in - run 'claude setup-token' or 'claude'"
        return None

    if _is_token_expired(oauth):
        # Autonomous refresh, same mechanisms as clawdmeter: the OAuth refresh-token
        # grant, then (windowless, rate-limited) letting Claude Code refresh its own
        # token. Both need a usable refresh path though — if the stored refreshToken
        # is empty AND a fresh `claude` can't renew (it 401s), nothing headless can,
        # and the durable fix is a long-lived token: `claude setup-token`.
        tok = _refresh_token(oauth, creds) or _refresh_via_claude_code()
        _auth_hint = "" if tok else "Token expired - run: claude setup-token (long-lived)"
        return tok

    _auth_hint = ""
    return oauth.get("accessToken")


# ---- API polling ----------------------------------------------------------

def poll_api(token: str) -> tuple[dict | None, bool]:
    """Minimal API call; extract usage headers. Returns (payload, auth_failed)."""
    headers = dict(API_HEADERS_TEMPLATE)
    headers["Authorization"] = f"Bearer {token}"
    try:
        resp = httpx.post(API_URL, headers=headers, json=API_BODY, timeout=20.0)
    except httpx.HTTPError as e:
        log(f"API call failed: {e}")
        return None, False
    if resp.status_code in (401, 403):
        return None, True
    if resp.status_code >= 400:
        log(f"API HTTP {resp.status_code}: {resp.text[:200]}")
        return None, False

    now = time.time()

    def hdr(name, default="0"):
        return resp.headers.get(name, default)

    def reset_minutes(ts):
        try:
            mins = (float(ts) - now) / 60.0
        except ValueError:
            return 0
        return int(round(mins)) if mins > 0 else 0

    def pct(util):
        try:
            return int(round(float(util) * 100))
        except ValueError:
            return 0

    payload = {
        "s":  pct(hdr("anthropic-ratelimit-unified-5h-utilization")),
        "sr": reset_minutes(hdr("anthropic-ratelimit-unified-5h-reset")),
        "w":  pct(hdr("anthropic-ratelimit-unified-7d-utilization")),
        "wr": reset_minutes(hdr("anthropic-ratelimit-unified-7d-reset")),
        "st": hdr("anthropic-ratelimit-unified-5h-status", "unknown"),
        "ok": True,
    }
    return payload, False


# ---- Poller (background thread) -------------------------------------------

def poller_loop(interval: float) -> None:
    log(f"Polling Claude every {interval:.0f}s")
    while not state.stop_event.is_set():
        token = read_token()
        if not token:
            state.set_status(_auth_hint or "No token - run 'claude' to log in")
            state.set_payload({"ok": False})
        else:
            payload, auth_failed = poll_api(token)
            if auth_failed:
                state.set_status("Refreshing token...")
                creds = _read_credentials_file()
                oauth = _get_oauth_block(creds) if creds else None
                if oauth and creds:
                    new_token = _refresh_token(oauth, creds)
                    if new_token:
                        payload, _ = poll_api(new_token)
            if payload is not None:
                state.set_payload(payload)
                state.set_status("Connected")
                log(f"5h={payload['s']}% 7d={payload['w']}% st={payload['st']}")
            elif "token" not in state.status.lower():
                state.set_status("API error — retrying")

        # Sleep until the interval elapses or a manual refresh is requested.
        state.refresh_event.wait(interval)
        state.refresh_event.clear()


# ---- HTTP server ----------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body: dict):
        data = json.dumps(body, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if self.path.rstrip("/") in ("/healthz", "/health"):
            self._send(200, {"ok": True})
            return
        self._send(200, state.get_payload())

    def log_message(self, *args):  # silence per-request logging
        pass


def start_http_server(host: str, port: int) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((host, port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# ---- System tray ----------------------------------------------------------

def _rgb565(c: int) -> tuple:
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def _make_icon_image(status_key: str):
    """Render the mascot pose into a tray icon, tinted by status."""
    from PIL import Image

    scale = 4
    n = len(MASCOT_ROWS)
    img = Image.new("RGBA", (n * scale, n * scale), (0, 0, 0, 0))
    px = img.load()
    for y, row in enumerate(MASCOT_ROWS):
        for x, ch in enumerate(row):
            idx = int(ch)
            if idx == 0:
                continue
            r, g, b = _rgb565(MASCOT_PALETTE[idx])
            if status_key == "searching":          # dim grey while waiting
                lum = (r * 30 + g * 59 + b * 11) // 100
                r = g = b = lum
            elif status_key == "error" and idx == 1:  # redden the body on error
                r, g, b = 200, 70, 55
            for dy in range(scale):
                for dx in range(scale):
                    px[x * scale + dx, y * scale + dy] = (r, g, b, 255)
    return img


def run_with_tray() -> None:
    import pystray

    icons = {k: _make_icon_image(k) for k in ("ok", "searching", "error")}

    def on_refresh(icon, item):
        state.refresh_event.set()

    def on_quit(icon, item):
        state.stop_event.set()
        state.refresh_event.set()
        icon.stop()

    menu = pystray.Menu(
        pystray.MenuItem(lambda _: state.get_tooltip(), None, enabled=False),
        pystray.Menu.SEPARATOR,
        pystray.MenuItem("Refresh now", on_refresh),
        pystray.MenuItem("Quit", on_quit),
    )
    icon = pystray.Icon("smalltv-usage", icon=icons["searching"],
                        title="smalltv usage — starting...", menu=menu)

    def updater():
        last = None
        while not state.stop_event.is_set():
            key = state.get_status_key()
            if key != last:
                last = key
                icon.icon = icons.get(key, icons["searching"])
            icon.title = state.get_tooltip()
            state.stop_event.wait(2)

    threading.Thread(target=updater, daemon=True).start()
    icon.run()
    state.stop_event.set()


def run_console() -> None:
    def _stop(*_):
        log("Stopping")
        state.stop_event.set()
        state.refresh_event.set()
    signal.signal(signal.SIGINT, _stop)
    try:
        signal.signal(signal.SIGTERM, _stop)
    except (ValueError, AttributeError):
        pass
    state.stop_event.wait()


# ---- Entry point ----------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description="Serve Claude usage to the SmallTV over HTTP.")
    ap.add_argument("--host", default=os.environ.get("SMALLTV_HOST", DEFAULT_HOST))
    ap.add_argument("--port", type=int, default=int(os.environ.get("SMALLTV_PORT", DEFAULT_PORT)))
    ap.add_argument("--interval", type=float, default=DEFAULT_POLL_INTERVAL,
                    help="seconds between Claude API refreshes (default 60)")
    ap.add_argument("--tray", action="store_true", help="show the tray icon (default)")
    ap.add_argument("--no-tray", action="store_true", help="run headless in the console")
    args = ap.parse_args()

    state.endpoint = f"http://{args.host}:{args.port}/"
    server = start_http_server(args.host, args.port)
    threading.Thread(target=poller_loop, args=(args.interval,), daemon=True).start()

    log(f"smalltv usage daemon on {state.endpoint}")
    log(f"Set the SmallTV's Usage URL to http://<this-pc-ip>:{args.port}/")

    use_tray = not args.no_tray
    if use_tray:
        try:
            import pystray  # noqa: F401
            from PIL import Image  # noqa: F401
            run_with_tray()
        except ImportError:
            log("pystray/Pillow not installed — running headless (pip install pystray Pillow)")
            run_console()
    else:
        run_console()

    server.shutdown()
    log("Stopped")


if __name__ == "__main__":
    main()
