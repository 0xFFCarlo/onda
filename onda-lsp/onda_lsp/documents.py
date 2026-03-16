"""In-memory text document store for the language server."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(slots=True)
class Document:
    uri: str
    text: str
    version: int | None


class DocumentStore:
    def __init__(self) -> None:
        self._docs: dict[str, Document] = {}

    def open(self, uri: str, text: str, version: int | None) -> None:
        self._docs[uri] = Document(uri=uri, text=text, version=version)

    def change(self, uri: str, text: str, version: int | None) -> None:
        if uri not in self._docs:
            self.open(uri=uri, text=text, version=version)
            return
        doc = self._docs[uri]
        doc.text = text
        doc.version = version

    def close(self, uri: str) -> None:
        self._docs.pop(uri, None)

    def get(self, uri: str) -> Document | None:
        return self._docs.get(uri)
