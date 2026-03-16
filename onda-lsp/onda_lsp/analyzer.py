"""Lightweight Onda tokenizer, symbol extraction, and diagnostics."""

from __future__ import annotations

from dataclasses import dataclass

from .types import Diagnostic, Position, Range, Symbol, Token

_BLOCK_OPENERS = {"if", "while"}
_NON_SYMBOL_TOKENS = {";", "if", "while", "then", "else", "end", "do"}


@dataclass(slots=True)
class AnalysisResult:
    diagnostics: list[Diagnostic]
    symbols: list[Symbol]
    tokens: list[Token]


def tokenize(text: str) -> tuple[list[Token], list[Diagnostic]]:
    tokens: list[Token] = []
    diagnostics: list[Diagnostic] = []

    for line_no, raw_line in enumerate(text.splitlines()):
        line = _strip_comment(raw_line)
        cursor = 0
        length = len(line)

        while cursor < length:
            if line[cursor].isspace():
                cursor += 1
                continue

            start = cursor
            if line[cursor] == '"':
                cursor = _consume_string(line, cursor)
                if cursor == -1:
                    diagnostics.append(
                        Diagnostic(
                            message="Unclosed string literal",
                            severity=1,
                            range=Range(
                                start=Position(line_no, start),
                                end=Position(line_no, len(line)),
                            ),
                        )
                    )
                    break
                continue

            while cursor < length and not line[cursor].isspace():
                cursor += 1
            text_token = line[start:cursor]
            tokens.append(
                Token(
                    text=text_token,
                    line=line_no,
                    start_char=start,
                    end_char=cursor,
                )
            )

    return tokens, diagnostics


def _consume_string(line: str, start: int) -> int:
    """Return index after closing quote; -1 when quote is not closed."""
    i = start + 1
    escaped = False
    while i < len(line):
        ch = line[i]
        if ch == '"' and not escaped:
            return i + 1
        if ch == "\\" and not escaped:
            escaped = True
        else:
            escaped = False
        i += 1
    return -1


def _strip_comment(line: str) -> str:
    in_string = False
    escaped = False
    for idx, ch in enumerate(line):
        if ch == '"' and not escaped:
            in_string = not in_string
        if ch == "#" and not in_string:
            return line[:idx]
        escaped = ch == "\\" and not escaped
        if ch != "\\":
            escaped = False
    return line


def analyze(text: str) -> AnalysisResult:
    tokens, diagnostics = tokenize(text)
    symbols: list[Symbol] = []

    block_stack: list[Token] = []
    open_word: Token | None = None

    for idx, token in enumerate(tokens):
        t = token.text

        if t in _BLOCK_OPENERS:
            block_stack.append(token)
        elif t == "end":
            if block_stack:
                block_stack.pop()
            else:
                diagnostics.append(
                    Diagnostic(
                        message="Unexpected 'end' with no matching block start",
                        severity=1,
                        range=Range(
                            start=Position(token.line, token.start_char),
                            end=Position(token.line, token.end_char),
                        ),
                    )
                )

        if t == ":":
            open_word = token
            if idx + 1 < len(tokens):
                name_tok = tokens[idx + 1]
                if name_tok.text not in _NON_SYMBOL_TOKENS:
                    symbols.append(
                        Symbol(
                            name=name_tok.text,
                            line=name_tok.line,
                            start_char=name_tok.start_char,
                            end_char=name_tok.end_char,
                        )
                    )
        elif t.startswith(":") and len(t) > 1:
            open_word = token
            symbols.append(
                Symbol(
                    name=t[1:],
                    line=token.line,
                    start_char=token.start_char,
                    end_char=token.end_char,
                )
            )
        elif t == ";":
            open_word = None

    for start in block_stack:
        diagnostics.append(
            Diagnostic(
                message=f"Unclosed block started with '{start.text}'",
                severity=1,
                range=Range(
                    start=Position(start.line, start.start_char),
                    end=Position(start.line, start.end_char),
                ),
            )
        )

    if open_word is not None:
        diagnostics.append(
            Diagnostic(
                message="Word definition is missing terminating ';'",
                severity=1,
                range=Range(
                    start=Position(open_word.line, open_word.start_char),
                    end=Position(open_word.line, open_word.end_char),
                ),
            )
        )

    return AnalysisResult(diagnostics=diagnostics, symbols=symbols, tokens=tokens)
