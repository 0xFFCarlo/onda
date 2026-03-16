"""Low-level JSON-RPC and LSP framing over stdio."""

from __future__ import annotations

import json
import sys
from typing import Any


class JsonRpcConnection:
    def __init__(self) -> None:
        self._stdin = sys.stdin.buffer
        self._stdout = sys.stdout.buffer

    def read_message(self) -> dict[str, Any] | None:
        headers: dict[str, str] = {}

        while True:
            line = self._stdin.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break

            if b":" not in line:
                continue

            key, value = line.decode("utf-8", errors="replace").split(":", 1)
            headers[key.strip().lower()] = value.strip()

        length = headers.get("content-length")
        if length is None:
            return None

        body = self._stdin.read(int(length))
        if not body:
            return None

        return json.loads(body.decode("utf-8"))

    def send(self, message: dict[str, Any]) -> None:
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        header = f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii")
        self._stdout.write(header)
        self._stdout.write(payload)
        self._stdout.flush()
