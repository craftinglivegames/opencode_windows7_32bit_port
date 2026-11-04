#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OCShell bridge agent.

Runs on the 64-bit host and starts ``opencode web`` on demand for the 32-bit
Windows 7 client (Supermium in app mode).  Only the Python standard library is
used.

Endpoints (bound to 0.0.0.0, default port 8011):
    GET  /status              -> {"running": bool, "url": "http://<ip>:8090", ...}
    POST /start?token=...     -> start opencode web if it is not running yet
    (X-Token: <token> header is accepted instead of the query parameter)

Config: dist/bridge.ini, section [bridge]:
    port          - this agent's HTTP port            (default 8011)
    token         - shared secret for /start          (default ocshell)
    opencode_path - path to opencode.exe, relative to the project root
                    or absolute                        (default dist\\portable\\opencode.exe)
    web_port      - port for ``opencode web``         (default 8090)

Logs: dist/bridge.log (child process output), dist/bridge_agent.log (agent).
"""

import configparser
import hmac
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(PROJECT_ROOT, "dist", "bridge.ini")
DIST_DIR = os.path.join(PROJECT_ROOT, "dist")
BRIDGE_LOG = os.path.join(DIST_DIR, "bridge.log")
AGENT_LOG = os.path.join(DIST_DIR, "bridge_agent.log")

DEFAULTS = {
    "port": "8011",
    "token": "ocshell",
    "opencode_path": os.path.join("dist", "portable", "opencode.exe"),
    "web_port": "8090",
}

START_TIMEOUT = 30.0
_child_lock = threading.Lock()
_child = None  # subprocess.Popen of the last started opencode web


def log(message):
    line = "%s %s" % (time.strftime("%Y-%m-%d %H:%M:%S"), message)
    print(line, flush=True)
    try:
        with open(AGENT_LOG, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")
    except OSError:
        pass


def load_config():
    cfg = dict(DEFAULTS)
    parser = configparser.ConfigParser()
    try:
        read = parser.read(CONFIG_PATH, encoding="utf-8")
        if read and parser.has_section("bridge"):
            for key in cfg:
                value = parser.get("bridge", key, fallback=None)
                if value is not None and value.strip():
                    cfg[key] = value.strip()
    except (configparser.Error, OSError) as exc:
        log("config: %s (using defaults)" % exc)
    try:
        cfg["port"] = int(cfg["port"])
        cfg["web_port"] = int(cfg["web_port"])
    except ValueError:
        log("config: bad port value, falling back to defaults")
        cfg["port"] = int(DEFAULTS["port"])
        cfg["web_port"] = int(DEFAULTS["web_port"])
    return cfg


def resolve_opencode(cfg):
    path = os.path.expanduser(cfg["opencode_path"])
    if not os.path.isabs(path):
        path = os.path.join(PROJECT_ROOT, path)
    return os.path.normpath(path)


def lan_ip():
    """Best-effort LAN address of this host (no traffic is actually sent)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        pass
    finally:
        sock.close()
    try:
        return socket.gethostbyname(socket.gethostname())
    except OSError:
        return "127.0.0.1"


def port_open(port, host="127.0.0.1", timeout=0.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def status_payload(cfg):
    running = port_open(cfg["web_port"])
    with _child_lock:
        child = _child
    pid = child.pid if (child is not None and child.poll() is None) else None
    payload = {
        "running": running,
        "url": "http://%s:%d" % (lan_ip(), cfg["web_port"]),
        "web_port": cfg["web_port"],
        "agent_port": cfg["port"],
    }
    if pid is not None:
        payload["pid"] = pid
    return payload


def start_opencode(cfg):
    """Start ``opencode web`` detached; returns (ok, detail)."""
    global _child

    exe = resolve_opencode(cfg)
    if not os.path.isfile(exe):
        return False, "opencode.exe not found: %s" % exe

    args = [exe, "web", "--hostname", "0.0.0.0", "--port", str(cfg["web_port"])]
    log("starting: %s" % " ".join(args))

    os.makedirs(DIST_DIR, exist_ok=True)
    logfile = open(BRIDGE_LOG, "ab", buffering=0)
    logfile.write(("\n=== %s start ===\n" % time.strftime("%Y-%m-%d %H:%M:%S")).encode("utf-8"))

    creationflags = 0
    if os.name == "nt":
        # Fully detached: survives agent shutdown, no console window.
        creationflags = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP

    try:
        child = subprocess.Popen(
            args,
            cwd=os.path.dirname(exe),
            stdin=subprocess.DEVNULL,
            stdout=logfile,
            stderr=subprocess.STDOUT,
            creationflags=creationflags,
            close_fds=True,
        )
    except OSError as exc:
        logfile.close()
        return False, "spawn failed: %s" % exc
    finally:
        pass

    with _child_lock:
        _child = child

    deadline = time.time() + START_TIMEOUT
    while time.time() < deadline:
        if port_open(cfg["web_port"]):
            log("opencode web is up (pid %d)" % child.pid)
            logfile.close()
            return True, "started"
        if child.poll() is not None:
            logfile.close()
            return False, "opencode exited with code %s, see %s" % (child.returncode, BRIDGE_LOG)
        time.sleep(0.4)

    # Still starting: keep the process, report the timeout.
    logfile.close()
    return port_open(cfg["web_port"]), "timeout after %ds, see %s" % (int(START_TIMEOUT), BRIDGE_LOG)


class BridgeHandler(BaseHTTPRequestHandler):
    server_version = "OCShellBridge/1.0"
    protocol_version = "HTTP/1.1"
    cfg = None

    def log_message(self, fmt, *args):
        log("%s %s" % (self.address_string(), fmt % args))

    # -- helpers ---------------------------------------------------------
    def _send_json(self, code, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "X-Token, Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()
        self.wfile.write(body)

    def _token(self, query):
        token = (self.headers.get("X-Token") or "").strip()
        if not token:
            values = query.get("token") or []
            if values:
                token = values[0].strip()
        return token

    # -- verbs -----------------------------------------------------------
    def do_OPTIONS(self):
        self._send_json(200, {"ok": True})

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path in ("/status", "/"):
            self._send_json(200, status_payload(self.cfg))
            return
        self._send_json(404, {"ok": False, "error": "unknown path"})

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path != "/start":
            self._send_json(404, {"ok": False, "error": "unknown path"})
            return

        query = parse_qs(parsed.query)
        token = self._token(query)
        expected = self.cfg["token"]
        if not token or not hmac.compare_digest(token, expected):
            log("start: rejected (bad token) from %s" % self.address_string())
            self._send_json(403, {"ok": False, "error": "bad token"})
            return

        cfg = self.cfg
        if port_open(cfg["web_port"]):
            payload = status_payload(cfg)
            payload["ok"] = True
            payload["started"] = False
            payload["detail"] = "already running"
            self._send_json(200, payload)
            return

        ok, detail = start_opencode(cfg)
        payload = status_payload(cfg)
        payload["ok"] = bool(ok and payload["running"])
        payload["started"] = ok
        payload["detail"] = detail
        self._send_json(200 if payload["ok"] else 503, payload)


def main():
    cfg = load_config()
    BridgeHandler.cfg = cfg
    os.makedirs(DIST_DIR, exist_ok=True)

    try:
        httpd = ThreadingHTTPServer(("0.0.0.0", cfg["port"]), BridgeHandler)
    except OSError as exc:
        log("cannot bind port %s: %s" % (cfg["port"], exc))
        return 2

    httpd.daemon_threads = True
    log("OCShell bridge listening on 0.0.0.0:%d -> opencode web port %d"
        % (cfg["port"], cfg["web_port"]))
    log("url: http://%s:%d  token: %s" % (lan_ip(), cfg["port"], "***" if cfg["token"] else "<empty>"))
    log("opencode: %s (exists=%s)" % (resolve_opencode(cfg), os.path.isfile(resolve_opencode(cfg))))
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log("stopping")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
