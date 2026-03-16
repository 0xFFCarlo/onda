# Onda LSP (Python-only)

A minimal Language Server Protocol (LSP) server for Onda, implemented in pure Python with no third-party dependencies.

## Current features

- LSP lifecycle: `initialize`, `initialized`, `shutdown`, `exit`
- Document sync: `textDocument/didOpen`, `didChange`, `didSave`, `didClose` (full sync)
- Diagnostics:
  - unclosed string literals
  - unmatched `end`
  - unclosed `if` / `while`
  - missing `;` after word definition
- Hover for Onda keywords, local words, and stdlib functions
- Hover includes stack effects, e.g. `( a b -- out )`
- Completion for core keywords, builtins, and stdlib functions
- Document symbols for word definitions (`: name ... ;` / `:name ... ;`)

## Run

```bash
cd onda-lsp
python -m onda_lsp
```

Or install in editable mode:

```bash
cd onda-lsp
pip install -e .
onda-lsp
```

## Notes

This architecture is intentionally simple so compiler-backed analysis (e.g. from `ondac`) can be added later behind the `analyzer` module.
