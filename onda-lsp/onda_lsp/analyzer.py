"""Lightweight Onda tokenizer, symbol extraction, and diagnostics."""

from __future__ import annotations

from dataclasses import dataclass

from .keywords import BUILTINS, KEYWORDS, STDLIB_FUNCTIONS
from .types import Diagnostic, Position, Range, Symbol, Token

_BLOCK_OPENERS = {"if", "while"}
_NON_SYMBOL_TOKENS = {";", "if", "while", "then", "else", "end", "do"}
_IMM_WORDS = {
    "+",
    "-",
    "*",
    "/",
    "%",
    "++",
    "--",
    "<<",
    ">>",
    "&",
    "|",
    "^",
    "~",
    "==",
    "!=",
    "<",
    "<=",
    ">",
    ">=",
    "over",
    "rot",
    "->",
    "continue",
    "@",
    "!",
    "b@",
    "b!",
    "h@",
    "h!",
    "w@",
    "w!",
}
_RESERVED_NAME_TOKENS = (
    set(KEYWORDS)
    | set(BUILTINS)
    | set(STDLIB_FUNCTIONS)
    | _IMM_WORDS
    | {"(", ")", "|", ":", ";"}
)


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
    word_defs: list[tuple[Token, list[Token]]] = []

    block_stack: list[Token] = []
    open_word: Token | None = None
    open_word_idx = -1

    for idx, token in enumerate(tokens):
        t = token.text

        if open_word is not None and idx > open_word_idx and t.startswith(":"):
            diagnostics.append(
                Diagnostic(
                    message="Nested word definition not allowed",
                    severity=1,
                    range=Range(
                        start=Position(token.line, token.start_char),
                        end=Position(token.line, token.end_char),
                    ),
                )
            )
            continue

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
            open_word_idx = idx
            if idx + 1 < len(tokens):
                name_tok = tokens[idx + 1]
                if name_tok.text not in _NON_SYMBOL_TOKENS:
                    sig_locals, _ = _parse_word_sig_locals(tokens, idx + 2)
                    symbols.append(
                        Symbol(
                            name=name_tok.text,
                            line=name_tok.line,
                            start_char=name_tok.start_char,
                            end_char=name_tok.end_char,
                        )
                    )
                    word_defs.append((name_tok, sig_locals))
        elif t.startswith(":") and len(t) > 1:
            if t.startswith("::"):
                continue
            open_word = token
            open_word_idx = idx
            name_tok = Token(
                text=t[1:],
                line=token.line,
                start_char=token.start_char + 1,
                end_char=token.end_char,
            )
            sig_locals, _ = _parse_word_sig_locals(tokens, idx + 1)
            symbols.append(
                Symbol(
                    name=name_tok.text,
                    line=name_tok.line,
                    start_char=name_tok.start_char,
                    end_char=name_tok.end_char,
                )
            )
            word_defs.append((name_tok, sig_locals))
        elif t == ";":
            open_word = None
            open_word_idx = -1

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

    diagnostics.extend(_validate_name_rules(word_defs))

    return AnalysisResult(diagnostics=diagnostics, symbols=symbols, tokens=tokens)


def _parse_word_sig_locals(tokens: list[Token], start_idx: int) -> tuple[list[Token], int]:
    if start_idx >= len(tokens) or tokens[start_idx].text != "(":
        return [], start_idx

    locals_tokens: list[Token] = []
    i = start_idx + 1
    while i < len(tokens):
        t = tokens[i].text
        if t == ")":
            return locals_tokens, i + 1
        if t != "|":
            locals_tokens.append(tokens[i])
        i += 1
    return locals_tokens, i


def _validate_name_rules(word_defs: list[tuple[Token, list[Token]]]) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    word_names: set[str] = set()
    first_word_tokens: dict[str, Token] = {}

    for word_tok, _ in word_defs:
        diagnostics.extend(_validate_name_token(word_tok, "Word"))
        if word_tok.text in first_word_tokens:
            diagnostics.append(
                _diag(
                    word_tok,
                    f"Word name '{word_tok.text}' is already defined",
                )
            )
        else:
            first_word_tokens[word_tok.text] = word_tok
        word_names.add(word_tok.text)

    for word_tok, local_tokens in word_defs:
        seen_locals: set[str] = set()
        for local_tok in local_tokens:
            diagnostics.extend(_validate_name_token(local_tok, "Local"))
            if local_tok.text in seen_locals:
                diagnostics.append(
                    _diag(
                        local_tok,
                        f"Local name '{local_tok.text}' is already defined",
                    )
                )
            else:
                seen_locals.add(local_tok.text)
            if local_tok.text in word_names:
                diagnostics.append(
                    _diag(
                        local_tok,
                        f"Local name '{local_tok.text}' conflicts with word name",
                    )
                )

    return diagnostics


def _validate_name_token(token: Token, kind: str) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    if ":" in token.text:
        diagnostics.append(
            _diag(
                token,
                f"{kind} name '{token.text}' cannot contain ':'",
            )
        )
    if token.text in _RESERVED_NAME_TOKENS:
        diagnostics.append(
            _diag(
                token,
                f"{kind} name '{token.text}' is reserved",
            )
        )
    return diagnostics


def _diag(token: Token, message: str) -> Diagnostic:
    return Diagnostic(
        message=message,
        severity=1,
        range=Range(
            start=Position(token.line, token.start_char),
            end=Position(token.line, token.end_char),
        ),
    )
