"""Core data structures used across server modules."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(slots=True)
class Position:
    line: int
    character: int


@dataclass(slots=True)
class Range:
    start: Position
    end: Position


@dataclass(slots=True)
class Diagnostic:
    message: str
    severity: int
    range: Range


@dataclass(slots=True)
class Symbol:
    name: str
    line: int
    start_char: int
    end_char: int


@dataclass(slots=True)
class Token:
    text: str
    line: int
    start_char: int
    end_char: int
