# Onda

**A tiny stack language for people who like control, speed, and small code.**

Onda is a low-level, Forth-inspired language with a practical toolchain:
- source compiler to bytecode
- VM runtime
- optional JIT backends
- imports, locals, control flow, and C-backed stdlib words

Onda is designed to stay simple enough to read end-to-end and fast enough to
use for real applications.

## Why Onda

- **Small mental model**: explicit stack behavior, left-to-right execution
- **Low-level power**: integer ops, memory access, C stdlib interop
- **Fast workflow**: run source, build bytecode, or execute bytecode
- **Performance path**: optional JIT
- **Tiny codebase**: built to remain understandable and hackable

## Quick Start

Build:

```bash
make
```

Run a program:

```bash
./bin/ondac run examples/hello_world.onda
```

Build bytecode + execute:

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
./bin/ondac run -e ':main "Hello from inline source\n" .s ;'
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

Current priority is not adding large new subsystems, but improving quality:
- fix bugs
- improve docs/examples
- sharpen performance and implementation details
- keep behavior stable and predictable

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
