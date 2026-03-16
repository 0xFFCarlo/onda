"""Main LSP server implementation for Onda."""

from __future__ import annotations

import traceback
from typing import Any

from .analyzer import analyze
from .documents import DocumentStore
from .keywords import BUILTINS, KEYWORDS, STACK_EFFECTS, STDLIB_FUNCTIONS
from .protocol import JsonRpcConnection
from .types import Diagnostic, Symbol, Token


class OndaLspServer:
    def __init__(self) -> None:
        self.conn = JsonRpcConnection()
        self.documents = DocumentStore()
        self.shutdown_requested = False

    def run(self) -> int:
        while True:
            msg = self.conn.read_message()
            if msg is None:
                return 0

            try:
                should_exit, exit_code = self._handle(msg)
                if should_exit:
                    return exit_code
            except Exception as exc:  # noqa: BLE001
                request_id = msg.get("id")
                if request_id is not None:
                    self.conn.send(
                        {
                            "jsonrpc": "2.0",
                            "id": request_id,
                            "error": {
                                "code": -32603,
                                "message": f"Internal error: {exc}",
                                "data": traceback.format_exc(),
                            },
                        }
                    )

    def _handle(self, msg: dict[str, Any]) -> tuple[bool, int]:
        method = msg.get("method")
        request_id = msg.get("id")
        params = msg.get("params") or {}

        if method == "initialize":
            self._reply(request_id, self._on_initialize())
            return False, 0

        if method == "initialized":
            return False, 0

        if method == "shutdown":
            self.shutdown_requested = True
            self._reply(request_id, None)
            return False, 0

        if method == "exit":
            return True, 0 if self.shutdown_requested else 1

        if method == "textDocument/didOpen":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            text = text_doc.get("text", "")
            version = text_doc.get("version")
            if uri:
                self.documents.open(uri=uri, text=text, version=version)
                self._publish_diagnostics(uri, text)
            return False, 0

        if method == "textDocument/didChange":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            version = text_doc.get("version")
            changes = params.get("contentChanges") or []
            if uri and changes:
                # Full sync: use the latest full text from the last change entry.
                text = changes[-1].get("text", "")
                self.documents.change(uri=uri, text=text, version=version)
                self._publish_diagnostics(uri, text)
            return False, 0

        if method == "textDocument/didSave":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            if uri:
                doc = self.documents.get(uri)
                if doc is not None:
                    self._publish_diagnostics(uri, doc.text)
            return False, 0

        if method == "textDocument/didClose":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            if uri:
                self.documents.close(uri)
                self._notify(
                    "textDocument/publishDiagnostics",
                    {"uri": uri, "diagnostics": []},
                )
            return False, 0

        if method == "textDocument/hover":
            result = self._on_hover(params)
            self._reply(request_id, result)
            return False, 0

        if method == "textDocument/completion":
            result = self._on_completion()
            self._reply(request_id, result)
            return False, 0

        if method == "textDocument/documentSymbol":
            result = self._on_document_symbol(params)
            self._reply(request_id, result)
            return False, 0

        if request_id is not None:
            self.conn.send(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {
                        "code": -32601,
                        "message": f"Method not found: {method}",
                    },
                }
            )
        return False, 0

    def _on_initialize(self) -> dict[str, Any]:
        return {
            "capabilities": {
                "textDocumentSync": 1,
                "hoverProvider": True,
                "completionProvider": {
                    "resolveProvider": False,
                    "triggerCharacters": [".", ":"],
                },
                "documentSymbolProvider": True,
            },
            "serverInfo": {
                "name": "onda-lsp",
                "version": "0.1.0",
            },
        }

    def _publish_diagnostics(self, uri: str, text: str) -> None:
        analysis = analyze(text)
        payload = {
            "uri": uri,
            "diagnostics": [self._diagnostic_to_lsp(d) for d in analysis.diagnostics],
        }
        self._notify("textDocument/publishDiagnostics", payload)

    def _on_hover(self, params: dict[str, Any]) -> dict[str, Any] | None:
        text_doc = params.get("textDocument", {})
        position = params.get("position", {})
        uri = text_doc.get("uri")
        line = position.get("line")
        char = position.get("character")

        if uri is None or line is None or char is None:
            return None

        doc = self.documents.get(uri)
        if doc is None:
            return None

        analysis = analyze(doc.text)
        token = _token_at(analysis.tokens, line, char)
        if token is None:
            return None

        info = (
            KEYWORDS.get(token.text)
            or BUILTINS.get(token.text)
            or STDLIB_FUNCTIONS.get(token.text)
        )
        stack = STACK_EFFECTS.get(token.text, "( ? -- ? )")
        if info is None:
            if any(sym.name == token.text for sym in analysis.symbols):
                info = "Word defined in this document."
                stack = "( ? -- ? )"
            else:
                return None

        return {
            "contents": {
                "kind": "markdown",
                "value": f"`{token.text}`\n\nStack: `{stack}`\n\n{info}",
            },
            "range": {
                "start": {"line": token.line, "character": token.start_char},
                "end": {"line": token.line, "character": token.end_char},
            },
        }

    def _on_completion(self) -> dict[str, Any]:
        items: list[dict[str, Any]] = []

        for name, info in KEYWORDS.items():
            items.append(
                {
                    "label": name,
                    "kind": 14,
                    "detail": "Onda keyword",
                    "documentation": info,
                }
            )

        for name, info in BUILTINS.items():
            items.append(
                {
                    "label": name,
                    "kind": 3,
                    "detail": "Onda builtin",
                    "documentation": info,
                }
            )

        for name, info in STDLIB_FUNCTIONS.items():
            if name in BUILTINS:
                continue
            items.append(
                {
                    "label": name,
                    "kind": 3,
                    "detail": "Onda stdlib",
                    "documentation": info,
                }
            )

        return {"isIncomplete": False, "items": items}

    def _on_document_symbol(self, params: dict[str, Any]) -> list[dict[str, Any]]:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri")
        if not uri:
            return []

        doc = self.documents.get(uri)
        if doc is None:
            return []

        analysis = analyze(doc.text)
        return [self._symbol_to_lsp(s) for s in analysis.symbols]

    def _reply(self, request_id: Any, result: Any) -> None:
        if request_id is None:
            return
        self.conn.send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def _notify(self, method: str, params: dict[str, Any]) -> None:
        self.conn.send({"jsonrpc": "2.0", "method": method, "params": params})

    @staticmethod
    def _diagnostic_to_lsp(diagnostic: Diagnostic) -> dict[str, Any]:
        return {
            "range": {
                "start": {
                    "line": diagnostic.range.start.line,
                    "character": diagnostic.range.start.character,
                },
                "end": {
                    "line": diagnostic.range.end.line,
                    "character": diagnostic.range.end.character,
                },
            },
            "severity": diagnostic.severity,
            "message": diagnostic.message,
            "source": "onda-lsp",
        }

    @staticmethod
    def _symbol_to_lsp(symbol: Symbol) -> dict[str, Any]:
        rng = {
            "start": {"line": symbol.line, "character": symbol.start_char},
            "end": {"line": symbol.line, "character": symbol.end_char},
        }
        return {
            "name": symbol.name,
            "kind": 12,
            "range": rng,
            "selectionRange": rng,
        }


def _token_at(tokens: list[Token], line: int, char: int) -> Token | None:
    for token in tokens:
        if token.line == line and token.start_char <= char < token.end_char:
            return token
    return None
