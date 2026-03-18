# Onda: Learn by Examples

This guide teaches Onda the same way you would learn it by hand: start with a
small program, then add one concept at a time until you can read and write
real multi-file programs.

At every step, keep this model in mind:
- Onda is stack-based.
- Words consume values from the top of the stack and push results back.
- Programs are mostly left-to-right token sequences.

## 1. Hello World

The first goal is to see the full shape of an Onda program: define `main`,
push a value, call a word.

File: `examples/hello_world.onda`

```onda
: main "Hello World!\n" .s ;
```

Step by step:
1. `: main ... ;` defines a word named `main`.
2. `"Hello World!\n"` pushes a pointer to the string literal.
3. `.s` prints that string and consumes the pointer.
4. Because `main` exists, it is used as program entry.

Run:

```bash
./bin/ondac run examples/hello_world.onda
```

## 2. Stack Basics

Now that you have a runnable program, focus on stack mechanics: arithmetic,
duplication, and printing.

File: `examples/stack_basics.onda`

```onda
: main
  3 4 + dup .
  " <- 3 + 4\n" .s
;
```

Step by step:
1. `3` pushes `3`.
2. `4` pushes `4`.
3. `+` pops `4` and `3`, then pushes `7`.
4. `dup` duplicates top value (`7` becomes `7 7`).
5. `.` prints one `7` and consumes it.
6. The second `7` stays on the stack.
7. `" <- 3 + 4\n" .s` prints the trailing message.

Why `dup` matters:
- You can inspect a value without losing it immediately.

Run:

```bash
./bin/ondac run examples/stack_basics.onda
```

## 3. Words With Arguments and Locals

As programs grow, unnamed stack juggling becomes harder to read. This section
shows Onda's readability pattern: named arguments plus named temporaries.

File: `examples/words_and_args.onda`

```onda
: hypot2 ( a b | aa bb )
  a a * -> aa
  b b * -> bb
  aa bb +
;

: main
  3 4 hypot2 .
  "\n" .s
;
```

Step by step (`hypot2`):
1. Signature `( a b | aa bb )` declares:
   - arguments: `a`, `b`
   - local temporaries: `aa`, `bb`
2. `a a *` loads `a` twice and computes `a²`.
3. `-> aa` stores that result into local `aa`.
4. `b b * -> bb` computes `b²` and stores in `bb`.
5. `aa bb +` loads locals and returns `a² + b²`.

Step by step (`main`):
1. Pushes `3`, `4`.
2. Calls `hypot2`, which returns `25`.
3. Prints `25`.

Practical rule:
- Use locals for conceptual intermediate values.
- Keep raw stack tricks for very small words.

Run:

```bash
./bin/ondac run examples/words_and_args.onda
```

## 4. Locals in `main`

You can apply the same style at the entry point. That keeps top-level logic
readable, especially in bigger scripts.

File: `examples/locals_and_main.onda`

```onda
: main ( | tmp )
  10 -> tmp
  tmp .
  "\n" .s
;
```

Step by step:
1. `( | tmp )` declares local `tmp` with no arguments.
2. `10` pushes `10`.
3. `-> tmp` stores top-of-stack into `tmp`.
4. `tmp` loads value back to stack.
5. `.` prints `10`.

Important:
- `main` supports locals.
- `main` does not accept arguments.

Run:

```bash
./bin/ondac run examples/locals_and_main.onda
```

## 5. Control Flow: `if`, `while`, `continue`

With naming in place, control flow reads like a direct algorithm. This example
sums odd numbers down from `n`.

File: `examples/control_flow.onda`

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

: main
  10 sum_odd_to .
  "\n" .s
;
```

Step by step (`sum_odd_to`):
1. Initialize accumulator: `0 -> acc`.
2. Loop while `n > 0`.
3. Check `n 2 % 0 ==` to detect even numbers.
4. If even, decrement `n` and `continue`.
5. If odd, add `n` into `acc`, then decrement `n`.
6. After loop, return `acc`.

For input `10`, output is `25` (`1+3+5+7+9`).

Run:

```bash
./bin/ondac run examples/control_flow.onda
```

## 6. Imports and Multi-file Programs

Once a word is useful, move it to a separate file and import it. That gives
you reuse without changing the language model.

Files:
- `examples/imports_math.onda`
- `examples/imports_main.onda`

`imports_math.onda`:

```onda
: square ( n ) n n * ;
: cube ( n ) n square n * ;
```

`imports_main.onda`:

```onda
import "imports_math.onda"

: main
  5 square .
  " " .s
  3 cube .
  "\n" .s
;
```

Step by step:
1. `import "imports_math.onda"` makes `square` and `cube` available.
2. `5 square` computes `25`.
3. `3 cube` computes `27`.
4. Program prints `25 27`.

Notes:
- Import paths are relative to the current file.
- Import cycles are rejected.

Run:

```bash
./bin/ondac run examples/imports_main.onda
```

## 7. Memory Basics

Onda stays low-level when you need it. This example shows explicit allocation,
store/load, and free.

File: `examples/memory_basics.onda`

```onda
: main ( | ptr )
  16 malloc -> ptr
  42 ptr !
  ptr @ .
  "\n" .s
  ptr free
;
```

Step by step:
1. `16 malloc` allocates 16 bytes and returns a pointer.
2. `-> ptr` stores pointer in a local.
3. `42 ptr !` stores 64-bit value `42` at `ptr`.
4. `ptr @` loads it back.
5. `.` prints `42`.
6. `ptr free` releases memory.

Typed memory operations:
- `b@/b!` byte, `h@/h!` halfword, `w@/w!` word, `@/!` 64-bit.

Run:

```bash
./bin/ondac run examples/memory_basics.onda
```

## 8. File I/O Basics

This is the same explicit style, now applied to I/O: open, check, write,
close.

File: `examples/file_io_basics.onda`

```onda
: main ( | fp )
  "/tmp/onda_example.txt" "w" fopen -> fp
  if fp not then
    "failed to open file\n" .s
    1 exit
  end

  "hello from onda\n" 1 16 fp fwrite drop
  fp fclose drop

  "wrote /tmp/onda_example.txt\n" .s
;
```

Step by step:
1. Open `/tmp/onda_example.txt` in write mode.
2. If open fails, print an error and exit.
3. `fwrite` writes 16 bytes from the string buffer.
4. `fclose` closes the file handle.
5. Final message confirms output path.

Run:

```bash
./bin/ondac run examples/file_io_basics.onda
```

## 9. Advanced Demos

After the core path, use these to study recursion depth, larger control flow,
and performance behavior.

- Recursion demo: `examples/run_fibonacci.onda`
- Solver: `examples/sudoku_solver.onda`
- Benchmarks: `examples/benchmark_1.onda`, `examples/benchmark_2.onda`

Run examples:

```bash
./bin/ondac run examples/run_fibonacci.onda
./bin/ondac run examples/sudoku_solver.onda
```

## 10. Build, Bytecode, and Execution Modes

You can run source directly for fast iteration, then switch to bytecode when
you want a build artifact.

Build tools:

```bash
make
```

Run source:

```bash
./bin/ondac run examples/hello_world.onda
```

Build bytecode:

```bash
./bin/ondac build examples/hello_world.onda /tmp/hello.onbc
```

Execute bytecode:

```bash
./bin/ondac exec /tmp/hello.onbc
```

VM-only execution (disable JIT):

```bash
./bin/ondac run --no-jit examples/hello_world.onda
```
