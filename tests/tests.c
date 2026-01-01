#include "../src/onda_comp_aarch64.h"
#include "../src/onda_jit.h"
#include "../src/onda_parser.h"
#include "../src/onda_vm.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CODE_BUF_SIZE 1024

typedef struct test_case_t {
  const char* program;
  size_t stack_size;
  int64_t expected_result_a;
  int64_t expected_result_b;
} test_case_t;

static const test_case_t tests[] = {
    // --- RET (empty stack)
    {"ret", 0, 0x0},

    // --- PUSH CONSTANTS
    {"10 ret", 1, 10},

    // --- PUSH MULTIPLE CONSTANTS
    {"10 20 ret", 2, 20, 10},

    // --- ENTRY POINT LABEL
    {"10 ret @main 42 ret", 1, 42},

    // --- ADD (+): minimal extra coverage (order shouldn't matter)
    {"2 4 + ret", 1, 6},

    // --- SUB (-): cover negative result explicitly
    {"4 1 - ret", 1, 3},
   
    // --- SUB (-): negative result
    {"1 4 - ret", 1, -3},

    // --- INC (++)
    {"41 ++ ret", 1, 42},

    // --- DEC (--)
    {"43 -- ret", 1, 42},

    // --- MULTIPLY (*)
    {"6 7 * ret", 1, 42},

    // --- DIVIDE (/): integer division assumed
    {"7 2 / ret", 1, 3},

    // --- MODULO (%)
    {"7 2 % ret", 1, 1},

    // --- NOT (!): assume logical-not (0 -> 1, nonzero -> 0)
    {"0 ! ret", 1, 1},
    {"5 ! ret", 1, 0},

    // --- EQUALITY (==)
    {"42 42 == ret", 1, 1},
    {"42 7 == ret", 1, 0},

    // --- NOT EQUAL (!=)
    {"42 7 != ret", 1, 1},
    {"42 42 != ret", 1, 0},

    // --- LESS (<)
    {"1 2 < ret", 1, 1},
    {"2 1 < ret", 1, 0},

    // --- LESS EQUAL (<=)
    {"2 2 <= ret", 1, 1},
    {"3 2 <= ret", 1, 0},

    // --- GREATER (>)
    {"2 1 > ret", 1, 1},
    {"1 2 > ret", 1, 0},

    // --- GREATER EQUAL (>=)
    {"2 2 >= ret", 1, 1},
    {"1 2 >= ret", 1, 0},

    // --- LOGICAL AND (and): assume nonzero=true, result 1/0
    {"1 1 and ret", 1, 1},
    {"1 0 and ret", 1, 0},

    // --- LOGICAL OR (or): assume nonzero=true, result 1/0
    {"0 0 or ret", 1, 0},
    {"1 0 or ret", 1, 1},

    // --- STACK OPS
    // drop: (a -- )
    {"10 drop ret", 0, 0},

    // dup: (a -- a a)
    {"7 dup ret", 2, 7, 7},

    // swap: (a b -- b a)
    {"10 20 swap ret", 2, 10, 20},

    // over: (a b -- a b a)  (copies 2nd item to top)
    {"10 20 over ret", 3, 10, 20},

    // rot: (a b c -- b c a)
    {"1 2 3 rot ret", 3, 2, 1},

    // --- jmp (unconditional) ---

    // Jump forward, skipping "111", land on label and push 222
    {"jmp end 111 ret @end 222 ret", 1, 222},

    // Jump forward to return immediately (skips pushing anything)
    {"jmp out 123 ret @out ret", 0, 0},

    // --- jmp_if (conditional) ---

    // Condition false: do not jump, returns 10
    {"0 jmp_if L 10 ret @L 20 ret", 1, 10},

    // Condition true: jump, returns 20
    {"1 jmp_if L 10 ret @L 20 ret", 1, 20},

    // Using a comparison
    {"10 15 > jmp_if cold 200 ret @cold 100 ret", 1, 200},

    // --- loop (uses both jmp_if + jmp) ---
    // Countdown loop: start at 3, decrement until 0, then return 0.
    // Stack stays 1-deep throughout.
    {"3 @loop dup 0 > jmp_if dec ret @dec -- jmp loop", 1, 0},
};

int main() {
  uint8_t codebuf[1024];
  size_t code_size = 0;
  size_t entry_pc = 0;
  size_t i;
  onda_vm_t* vm = onda_vm_new();
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;

  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    const test_case_t* tc = &tests[i];
    onda_parse(tc->program, codebuf, CODE_BUF_SIZE, &code_size, &entry_pc);
    onda_vm_load_code(vm, codebuf, entry_pc, code_size);
    onda_vm_run(vm);
    if (vm->sp != tc->stack_size) {
      fprintf(stderr,
              "Test %zu failed: expected stack size %zu, got %zu\n",
              i,
              tc->stack_size,
              vm->sp);
      goto failed;
    }
    if (tc->stack_size > 0) {
      int64_t val = vm->stack[vm->sp - 1];
      if (val != tc->expected_result_a) {
        fprintf(stderr,
                "Test %zu failed: expected TOS %llu, got %llu\n",
                i,
                tc->expected_result_a,
                val);
        goto failed;
      }
    }
    if (tc->stack_size > 1) {
      int64_t val = vm->stack[vm->sp - 2];
      if (val != tc->expected_result_b) {
        fprintf(stderr,
                "Test %zu failed: expected TOS-1 %llu, got %llu\n",
                i,
                tc->expected_result_b,
                val);
        goto failed;
      }
    }

    // JIT test
    onda_comp_aarch64(codebuf,
                      entry_pc,
                      code_size,
                      &machine_code,
                      &machine_code_size);
    uint64_t tos = onda_jit_run(machine_code, machine_code_size);
    if (tc->stack_size > 0) {
      if (tos != tc->expected_result_a) {
        fprintf(stderr,
                "Test %zu JIT failed: expected TOS %llu, got %llu\n",
                i,
                tc->expected_result_a,
                tos);
        printf("Machine code:\n");
        for (size_t j = 0; j < machine_code_size; j++) {
          if (j % 16 == 0 && j != 0)
            printf("\n");
          printf("%02X ", machine_code[j]);
        }
        printf("\n");
        goto failed;
      }
    }

    printf("Test %zu passed.\n", i);
  }

  onda_vm_free(vm);

  return 0;
failed:
  printf("Failed program:\n %s\n", tests[i].program);
  return 1;
}
