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
: hypot2 ( a b ) [ aa bb ]
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
1. Signature `( a b ) [ aa bb ]` declares:
   - arguments: `a`, `b`, in `( )`
   - local temporaries: `aa`, `bb`, in `[ ]`
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
: main [ tmp ]
  10 -> tmp
  tmp .
  "\n" .s
;
```

Step by step:
1. `[ tmp ]` declares local `tmp`, with no arguments needed since `( )` is omitted.
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

## 5. Control Flow: `if`, `while`, `next`

With naming in place, control flow reads like a direct algorithm. This example
sums odd numbers down from `n`.

File: `examples/control_flow.onda`

```onda
: sum_odd_to ( n ) [ acc ]
  0 -> acc
  while n 0 > do
    if n 2 % 0 == then
      n -- -> n
      next
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
4. If even, decrement `n` and `next` (skip to the next loop iteration).
5. If odd, add `n` into `acc`, then decrement `n`.
6. After loop, return `acc`.

For input `10`, output is `25` (`1+3+5+7+9`).

Run:

```bash
./bin/ondac run examples/control_flow.onda
```

## 6. Operators and Comments

Section 2 covered `+` and `dup`. Here is the rest of the operator set, plus
comments and alternate number literals.

File: `examples/operators_and_comments.onda`

```onda
# Lines starting with '#' are comments.
# Numbers can also be written in hex (0x..) or binary (0b..).

: main
  0xff 0b1010 & .    # bitwise AND: 255 & 10 = 10
  " <- 0xff & 0b1010\n" .s

  1 4 << .           # shift left: 1 << 4 = 16
  " <- 1 << 4\n" .s

  5 3 >= .           # comparisons push 1 (true) or 0 (false)
  " <- 5 >= 3\n" .s

  1 1 == 0 1 == or . # logical or: true or false = 1
  " <- (1==1) or (0==1)\n" .s

  1 2 3 rot          # rot moves top-of-stack down to third place: 1 2 3 -> 2 1 3
  . " " .s . " " .s .
  " <- 1 2 3 rot\n" .s
;
```

Full operator reference:
- Arithmetic: `+ - * / % ++ --`
- Bitwise: `& | ^ ~ << >>`
- Comparison: `== != < <= > >=` (push `1` for true, `0` for false)
- Logical: `and or not`
- Stack shuffling: `dup drop swap over rot`

Run:

```bash
./bin/ondac run examples/operators_and_comments.onda
```

## 7. Macros: `::`

`::` defines a macro (Onda calls it an alias): its body is expanded inline at
every call site instead of being compiled as a callable word. That makes it
useful for named constants and for small snippets that should carry zero call
overhead.

File: `examples/macros_basics.onda`

```onda
# Macros defined with `::` are expanded inline at every call site.
# They cannot declare their own `( args )` or `[ locals ]`, so a macro
# body only ever sees whatever the *caller* has on the stack or in scope.

:: TAX_RATE 20 ;
:: DOUBLE dup + ;
:: QUADRUPLE DOUBLE DOUBLE ;

:: with_tax dup TAX_RATE * 100 / + ;

: main [ price ]
  50 -> price

  price with_tax .
  " <- 50 plus 20% tax\n" .s

  3 QUADRUPLE .
  " <- 3 doubled twice\n" .s
;
```

Step by step:
1. `:: TAX_RATE 20 ;` defines a constant-style macro: every use of `TAX_RATE`
   expands to `20`.
2. `:: DOUBLE dup + ;` defines a code-style macro; `QUADRUPLE` expands to
   `DOUBLE DOUBLE`, which itself expands to `dup + dup +`.
3. `with_tax` uses `TAX_RATE` and operates directly on whatever value the
   caller left on the stack — it has no locals of its own.
4. In `main`, `price with_tax` and `3 QUADRUPLE` compile as if their bodies
   were pasted in directly.

Rules and restrictions:
- A macro cannot declare `( args )` or `[ locals ]`.
- A macro body is expanded textually, so it can only read/write the caller's
  stack and the caller's own locals (by name, if the caller happens to have
  one with a matching name).
- Macros can call other macros; expansion depth is capped to catch infinite
  recursion.
- `pub ::name ... ;` exports a macro the same way `pub :name ... ;` exports a
  word.

When to reach for a macro instead of a word:
- Small named constants (`TAX_RATE`, `BOARD_SIZE`).
- Tiny snippets used in hot loops where you want to avoid `call`/`ret`
  overhead (see `examples/sudoku_solver.onda` and
  `examples/labels_and_jump_dispatch.onda` for real uses).

Run:

```bash
./bin/ondac run examples/macros_basics.onda
```

## 8. Modules and Multi-file Programs

Once a word is useful, move it to a separate file and pull it in with `use`.
Words are private by default, so a word must be marked `pub` before another
file can call it.

Files:
- `examples/imports_math.onda`
- `examples/imports_main.onda`

`imports_math.onda`:

```onda
pub : square ( n ) n n * ;
pub : cube ( n ) n square n * ;
```

`imports_main.onda`:

```onda
use "imports_math.onda"

: main
  5 square .
  " " .s
  3 cube .
  "\n" .s
;
```

Step by step:
1. `use "imports_math.onda"` makes `square` and `cube` available.
2. `pub` on their definitions is what allows them to be visible outside their file.
3. `5 square` computes `25`.
4. `3 cube` computes `27`.
5. Program prints `25 27`.

Notes:
- `use` paths are relative to the current file.
- `use` cycles are rejected.

Run:

```bash
./bin/ondac run examples/imports_main.onda
```

## 9. Memory Basics

Onda stays low-level when you need it. This example shows explicit allocation,
store/load, and free.

File: `examples/memory_basics.onda`

```onda
: main [ ptr ]
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

## 10. File I/O Basics

This is the same explicit style, now applied to I/O: open, check, write,
close.

File: `examples/file_io_basics.onda`

```onda
: main [ fp ]
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

## 11. Computed Control Flow: `label` and `jump`

Onda supports computed jumps through label addresses. In the current compiler,
labels are resolved backward-only, so jump targets must be declared earlier in
the same word.

```onda
: count_down_with_jump ( n ) [ i ]
  n -> i

  label loop
  i .
  "\n" .s
  i -- -> i
  if i 0 > then loop jump end

  i
;

: main
  5 count_down_with_jump drop
;
```

How this works:
1. `label loop` records the current instruction address.
2. Later, `loop` pushes that address to the stack.
3. `jump` pops the address and branches to it.
4. The loop continues while `i > 0`.

Practical notes:
- Labels are local to the word where they are declared.
- In current Onda, labels must be referenced after declaration.
- Use `if`/`while` for everyday flow, and `jump` for low-level control.

## 12. Standard Library Quick Reference

Beyond `malloc`/`free` and `fopen`/`fwrite`/`fclose`, Onda's C-backed stdlib
covers the rest of the usual low-level toolkit. All of it is called like any
other word — push arguments, call the name, results come back on the stack.

Printing:
- `.` prints an integer, `.h` prints hex, `.c` prints a byte as a char.
- `.s` prints a string, `.nl` prints a newline, `.p` prints a raw pointer.
- `depth` pushes the current stack depth, `.stack` prints the whole stack.

Memory:
- `malloc calloc realloc free`
- `memcpy memset memcmp`

Strings:
- `strlen strcmp strncmp strcpy strncpy strcat strncat strchr strstr`
- `atoi strtol strtoul` parse numbers out of strings.

Files:
- `fopen fclose fread fwrite fseek ftell fflush feof ferror rewind clearerr`
- `remove rename tmpfile`

Misc:
- `exit` ends the process with the given status code.
- `assert` checks a condition and aborts with a message if it fails.

## 13. Advanced Demos

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

## 14. Build, Bytecode, and Execution Modes

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
