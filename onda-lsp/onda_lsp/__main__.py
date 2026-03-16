"""CLI entrypoint for the Onda language server."""

from __future__ import annotations

import sys

from .server import OndaLspServer


def main() -> None:
    server = OndaLspServer()
    code = server.run()
    raise SystemExit(code)


if __name__ == "__main__":
    main()
