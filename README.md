# Onda

**A tiny stack language with a JIT-first runtime and a small implementation.**

Onda is a low-level, Forth-inspired language with a practical toolchain:
- source compiler to bytecode
- JIT execution by default on arm64 and x86-64
- VM execution for fallback and debugging
- imports, locals, control flow, and C-backed stdlib words

Onda is built for people who want direct control, fast execution, and a
language implementation that stays small. The whole language and toolchain
currently fit in roughly 4-5k lines of code.

## Why Onda

- **JIT-first execution**: the normal path is compiled machine code, not an interpreter
- **VM fallback**: `--no-jit` is there for portability, debugging, and comparison
- **Low-level control**: integer ops, memory access, explicit stack behavior
- **Real toolchain**: run source, build bytecode, or execute bytecode directly
- **Small codebase**: core language and runtime stay in the 4-5k LOC range

## First Look

```onda
: main "hello, onda\n" .s ;
```

Run it:

```bash
./bin/ondac run -e ': main "hello, onda\n" .s ;'
```

What this project is aiming for:
- a compact stack language with real control flow and locals
- bytecode as a stable compilation target
- JIT as the preferred runtime path

## Quick Start

Build:

```bash
make
```

Run a program with the default JIT path:

```bash
./bin/ondac run examples/hello_world.onda
```

Run the same program on the VM:

```bash
./bin/ondac run --no-jit examples/hello_world.onda
```

Build bytecode + execute it:

```bash
./bin/ondac build examples/hello_world.onda /tmp/hello.onbc
./bin/ondac exec /tmp/hello.onbc
```

## Language Taste

```onda
: sum_odd_to ( n | acc )
  0 -> acc
  while n 0 > do
    if n 2 % 0 == then
      n -- -> n
      continue
    end
    acc n + -> acc
    n -- -> n
  end
  acc
;

: main 10 sum_odd_to . "\n" .s ;
```

## CLI

```text
ondac run   [--no-jit] [--time] [--print-bytecode] (-e <source_string> | <source_file>)
ondac build [--time] [--print-bytecode] <source_file> <output_bytecode>
ondac exec  [--no-jit] [--time] [--print-bytecode] <bytecode_file>
```

Inline source example:

```bash
./bin/ondac run -e ': main "Hello from inline source\n" .s ;'
```

Use the VM instead of the JIT:

```bash
./bin/ondac run --no-jit examples/hello_world.onda
```

## Learn

- Guided walkthrough: [`LEARN_BY_EXAMPLES.md`](LEARN_BY_EXAMPLES.md)
- Runnable programs: [`examples/`](examples)

Recommended first runs:
- `./bin/ondac run examples/hello_world.onda`
- `./bin/ondac run examples/stack_basics.onda`
- `./bin/ondac run examples/locals_and_main.onda`
- `./bin/ondac run examples/control_flow.onda`

Advanced demos:
- `./bin/ondac run examples/run_fibonacci.onda`
- `./bin/ondac run examples/sudoku_solver.onda`
- `./bin/ondac run examples/benchmark_1.onda`

If you want the fastest sense of what Onda can do, start with:
- `examples/control_flow.onda`
- `examples/file_io_basics.onda`
- `examples/sudoku_solver.onda`

## Install

Install to `~/.local/bin`:

```bash
make install
```

Uninstall:

```bash
make uninstall
```

## Project Status

Onda is **mostly feature complete**.

The direction is to keep the language small, keep JIT execution as the default,
and improve the parts that matter in practice:
- fix bugs and edge cases
- improve docs and examples
- sharpen code generation and runtime behavior
- keep semantics stable and predictable

## Contributing

Contributions are welcome, especially:
- bug fixes
- performance improvements
- readability and tooling improvements
- docs and example improvements

Project constraints:
- keep the implementation small
- target **under 5k LOC** for core language/runtime sources
- preserve a **minimal footprint** in code and runtime behavior
- prefer the smallest change that solves the problem

Before opening a PR:

```bash
make
./bin/tests_ondac
```

When proposing larger changes, include a short rationale for why they still fit
Onda's minimal-footprint goals.

## Editor and Tooling

- Vim syntax/ftdetect: [`vim/`](vim)
- Python LSP server: [`onda-lsp/`](onda-lsp)

## Project Layout

```text
src/         compiler, VM, optimizer, JIT backends, stdlib bindings
examples/    runnable Onda programs
tests/       C test suite
onda-lsp/    standalone Python language server
vim/         editor integration files
```

If you want to inspect the implementation quickly, start in `src/main.c` for
the CLI, then move to the compiler, VM, and JIT sources under `src/`.
