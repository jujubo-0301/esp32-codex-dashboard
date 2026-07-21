from __future__ import annotations

import argparse
import ipaddress
import json
import socket
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parent
STATUS_FILE = ROOT / "status.json"
LOCK = threading.Lock()


def advertised_ip() -> str:
    """Return the private LAN address the board should use for HTTP."""
    candidates = []
    try:
        for entry in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            candidates.append(entry[4][0])
    except OSError:
        pass
    for value in candidates:
        try:
            address = ipaddress.ip_address(value)
            if address.is_private and not address.is_loopback and not address.is_link_local:
                return value
        except ValueError:
            continue
    return "0.0.0.0"


def discovery_responder() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", 8788))
    while True:
        payload, address = sock.recvfrom(128)
        if payload.strip() == b"CODEX_DISCOVER":
            reply = f"CODEX_BRIDGE 8787 {advertised_ip()}".encode("ascii")
            sock.sendto(reply, address)


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def read_status() -> dict:
    with LOCK:
        try:
            return json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError):
            return {
                "state": "offline",
                "overall_state": "offline",
                "task": "",
                "step": "",
                "progress": 0,
                "overall_progress": 0,
                "overall_elapsed_s": 0,
                "overall_task_count": 0,
                "step_index": 0,
                "step_total": 0,
                "cpu": 0,
                "ram": 0,
                "gpu": 0,
                "summary": "",
                "clock": "--:--",
                "tasks": [],
                "quota_remaining": "--",
                "reset_date": "--",
                "elapsed_s": 0,
                "message": "等待电脑端状态服务",
                "error": "",
                "updated_at": now_iso(),
            }


def write_status(payload: dict) -> None:
    payload = dict(payload)
    payload["updated_at"] = now_iso()
    with LOCK:
        STATUS_FILE.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


class Handler(BaseHTTPRequestHandler):
    def _json(self, code: int, payload: dict) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/health":
            self._json(200, {"ok": True, "updated_at": now_iso()})
            return
        if self.path in ("/", "/status"):
            self._json(200, read_status())
            return
        self._json(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/status":
            self._json(404, {"ok": False, "error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("payload must be an object")
            write_status(payload)
            self._json(200, {"ok": True, "status": read_status()})
        except (ValueError, TypeError, json.JSONDecodeError) as exc:
            self._json(400, {"ok": False, "error": str(exc)})

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[bridge] {fmt % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Local status bridge for the ESP32 Codex display")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    threading.Thread(target=discovery_responder, name="codex-discovery", daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Status bridge listening on http://{args.host}:{args.port}/status")
    server.serve_forever()


if __name__ == "__main__":
    main()
