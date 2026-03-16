# Onda: Learn by Examples

This guide explains Onda by walking from simple examples to advanced features.

Onda is a Forth-like, stack-based language:
- You push values to a data stack.
- Words (functions) consume and produce stack values.
- Programs are mostly sequences of tokens executed left-to-right.

## 1. First program

From `examples/hello_world.onda`:

```onda
: main "Hello World!\n" .s ;
```

What happens:
- `: main ... ;` defines a word named `main`.
- The string literal pushes a pointer to the string.
- `.s` prints the string.
- `main` is used as the entry point when present.

## 2. Core syntax

### Comments

Use `#` for comments to end-of-line:

```onda
# This is a comment
: main 1 2 + . ; # trailing comment
```

### Strings

Strings are in double quotes.
Escape sequences supported by lexer/compiler include:
- `\n`, `\t`, `\r`, `\"`, `\\`

```onda
"Line1\nLine2\tTabbed"
```

### Numbers

Integer literals supported:
- Decimal: `42`, `-7`
- Hex: `0xFF`
- Binary: `0b1011`

## 3. Defining words (Forth-style)

### Word with no arguments

```onda
: square dup * ;
: main 5 square . ;
```

Pattern: `: name ... ;`

### Word with arguments

From `examples/words_with_args.onda`:

```onda
: dist ( a b ) a a * b b * + ;
: main 3 4 dist . ;
```

- `( a b )` declares named inputs for readability.
- Inside body, `a` and `b` push those local values.

### Word with arguments and locals

From `examples/words_with_args_2.onda`:

```onda
: dist2 ( a b | aa bb )
  a a * -> aa
  b b * -> bb
  aa bb + ;
```

- `|` splits argument names from local-only temporaries.
- `-> name` stores top-of-stack into an existing local/argument.

### Word with only locals

You can define locals without arguments:

```onda
: example ( | tmp count )
  10 -> count
  count 2 * -> tmp
  tmp .
;
```

### Nested word definitions are not allowed

You cannot define `: another ... ;` inside a word body.

## 4. Calling words and stack behavior

From `examples/fibonacci.onda` and `examples/run_fibonacci.onda`:

```onda
:fib
  if dup 2 < then
    if dup 0 == then drop 0 else drop 1 end
  else
    dup 1 - fib swap 2 - fib +
  end
;

:main 10 fib . ;
```

Notes:
- Words can be written as `:fib ... ;` or `: fib ... ;`.
- Stack operators like `dup`, `swap`, `drop` are central in Onda.

## 5. Conditionals

From `examples/if_statement_example.onda`:

```onda
: main
  if 10 15 > then
    "Temperature is cold\n" .s
    ret
  else
    "Temperature is warm\n" .s
    ret
  end
;
```

Syntax:
- `if <condition-code> then <true-branch> [else <false-branch>] end`

The condition code runs first and leaves a truthy/falsy value on stack.

## 6. Loops and continue

From `examples/count_down.onda`:

```onda
: main
  10
  while dup 0 > do
    dup . -- "\n" .s
  end
  drop
;
```

Syntax:
- `while <condition-code> do <body> end`

`continue` exists and jumps to the current loop condition check:

```onda
: demo
  10
  while dup 0 > do
    if dup 5 == then
      --
      continue
    end
    dup .
    --
  end
  drop
;
```

`continue` is only valid inside a `while` loop.

## 7. Imports and multi-file programs

From `examples/run_fibonacci.onda`:

```onda
import "fibonacci.onda"

:main 10 fib . ;
```

Notes:
- `import` requires a string literal path.
- Imports resolve relative to the current file path.
- Import cycles are detected (see `examples/import_cycle_*.onda`).

## 8. Alias words (`::`)

Aliases are compile-time expansions.

Syntax:

```onda
:: inc10 10 + ;
: main 5 inc10 . ;
```

Important rules:
- Alias definition starts with `:: name ... ;`
- Aliases cannot declare argument/local lists (`(`, `)`, `|` are forbidden there).
- Aliases expand recursively at compile time (with depth limits to prevent infinite recursion).

Use alias when you want a short reusable phrase, not a real call frame.

## 9. Operators and memory words

Common immediate words/operators include:
- Arithmetic: `+ - * / % ++ --`
- Comparison: `== != < <= > >=`
- Logic: `and or not`
- Bitwise: `& | ^ ~ << >>`
- Stack: `drop dup over rot swap ret`

Memory load/store words (typed widths):
- Byte: `b@` / `b!`
- Halfword: `h@` / `h!`
- Word: `w@` / `w!`
- 64-bit: `@` / `!`

From `examples/memory.onda`:

```onda
: main 16 malloc dup 12 swap ! dup @ . free ;
```

## 10. Standard library (C-backed) words

Onda auto-registers a standard native library from C (`src/onda_std.c`).
Examples include:
- Printing: `.`, `.s`, `.c`, `print_u64`, `print_i64`, `print_hex`
- Memory/string: `malloc`, `calloc`, `free`, `memcpy`, `memset`, `strlen`, `strcmp`, ...
- File I/O: `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`, `fflush`, ...
- Misc: `exit`, `assert`, `remove`, `rename`, `tmpfile`

You can see usage in examples like:
- `examples/file_test.onda`
- `examples/sudoku.onda`

## 11. C interop: adding your own native function

Onda can call C functions registered in the native registry.

### C callback signature

```c
typedef int64_t* (*onda_native_fn_cb_t)(int64_t* data_stack);
```

Your function receives a pointer to the top-of-stack (`sp`) and returns the new top-of-stack after consuming args and producing returns.

### Registering a function

Use:

```c
int onda_env_register_native_fn(onda_env_t* env,
                                const char* name,
                                onda_native_fn_cb_t fn,
                                uint8_t args_count,
                                uint8_t returns_count);
```

Minimal example:

```c
static int64_t* native_add2(int64_t* sp) {
  // Stack top is last pushed value.
  // Onda: 3 4 add2  => *sp=4, *(sp+1)=3
  *(sp + 1) = *(sp + 1) + *sp;
  return sp + 1; // consumed 2, produced 1
}

// During env setup:
onda_env_register_native_fn(&env, "add2", native_add2, 2, 1);
```

Then call from Onda:

```onda
: main 3 4 add2 . ;
```

### Interop conventions

- Onda resolves unknown identifiers against native function registry.
- `args_count` and `returns_count` are metadata used by the VM/compiler ecosystem.
- Keep stack discipline correct in native callbacks, or runtime behavior will break.

## 12. Running programs

Build compiler:

```bash
make
```

Run source:

```bash
./bin/ondac run examples/hello_world.onda
```

Build bytecode:

```bash
./bin/ondac build examples/fibonacci.onda /tmp/fib.onbc
```

Execute bytecode:

```bash
./bin/ondac exec /tmp/fib.onbc
```

Useful flags:
- `--time`
- `--print-bytecode`
- `--no-jit` (for `run`/`exec`)

## 13. Practical learning path from repo examples

1. `examples/hello_world.onda` (word + string output)
2. `examples/words2.onda` (basic stack math)
3. `examples/words_with_args*.onda` (args, locals, `->`)
4. `examples/if_statement_example.onda` (conditionals)
5. `examples/count_down.onda` (while loop)
6. `examples/fibonacci.onda` + `run_fibonacci.onda` (composition/import)
7. `examples/memory*.onda` (memory ops)
8. `examples/file_test.onda` / `sudoku.onda` (C stdlib interop usage)
