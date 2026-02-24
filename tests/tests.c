#include "../src/onda_comp_aarch64.h"
#include "../src/onda_compiler.h"
#include "../src/onda_jit.h"
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
  bool debug_mode;
} test_case_t;

static const test_case_t tests[] = {
    // --- RET (empty stack)
    {"ret", 0, 0x0},

    // --- PUSH CONSTANTS
    {"10 ret", 1, 10},

    // --- PUSH MULTIPLE CONSTANTS
    {"10 20 ret", 2, 20, 10},

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

    // If condition
    {"if 1 then 2 endif ret", 1, 2},
    {"if 0 then 2 endif ret", 0, 0},
    {"if 2 3 > then 4 4 + else 5 5 + endif ret", 1, 10},
    {"if 2 3 < then 4 4 + else 5 5 + endif ret", 1, 8},
    {"if 1 then 3 else 4 endif ret", 1, 3},
    {"if 0 then 3 else 4 endif ret", 1, 4},

    // While loop
    {"10 while dup 2 > do -- endwhile ret", 1, 2},
    {"5 while dup 0 > do 1 - endwhile drop ret", 0, 0},

    // Words
    {":square  dup * ; "
     ":main    5 square ; ",
     1,
     25},
    {":factorial  if dup 1 <= then drop 1 else dup 1 - factorial * endif ; "
     ":main       5 factorial ; ",
     1,
     120},
    {":fun_c  3 ; "
     ":fun_b  2 fun_c + ; "
     ":fun_a  1 fun_b + ; "
     ":main   fun_a ; ",
     1,
     6},
    // Words with arguments
    {": dist ( a b ) a a * b b * + ;"
     ": main 3 4 dist ;",
     1, 25},
    {": const ( a ) 10 -> a a ;"
     ": main 0 const ;",
     1, 10},
};

int main() {
  onda_lexer_t lexer;
  onda_code_obj_t cobj = {0};
  size_t i;
  onda_vm_t* vm = onda_vm_new();
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;
  uint64_t frame_stack[ONDA_VM_FRAME_STACK_SIZE];

  // Run tests using VM
  printf("Testing with VM:\n");
  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    onda_code_obj_init(&cobj, CODE_BUF_SIZE);
    cobj.size = 0;
    cobj.entry_pc = 0;
    lexer.src = tests[i].program;
    lexer.column = 0;
    lexer.line = 0;
    lexer.pos = 0;
    const test_case_t* tc = &tests[i];
    if (onda_compile(&lexer, &cobj) != 0) {
      fprintf(stderr, "Test %zu failed: compilation error\n", i);
      goto failed;
    }
    // onda_dict_free(&cobj.words_map);
    onda_vm_load_code(vm, cobj.code, cobj.entry_pc, cobj.size);
    vm->debug_mode = tc->debug_mode;
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
      int64_t val = vm->data_stack[vm->sp - 1];
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
      int64_t val = vm->data_stack[vm->sp - 2];
      if (val != tc->expected_result_b) {
        fprintf(stderr,
                "Test %zu failed: expected TOS-1 %llu, got %llu\n",
                i,
                tc->expected_result_b,
                val);
        goto failed;
      }
    }

    onda_code_obj_free(&cobj);
    printf("Test %zu passed.\n", i);
  }

  onda_vm_free(vm);

  // Run tests using JIT only (without VM execution)
  printf("\nTesting with JIT:\n");
  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    onda_code_obj_init(&cobj, CODE_BUF_SIZE);
    cobj.size = 0;
    cobj.entry_pc = 0;
    lexer.src = tests[i].program;
    lexer.column = 0;
    lexer.line = 0;
    lexer.pos = 0;
    const test_case_t* tc = &tests[i];
    if (onda_compile(&lexer, &cobj) != 0) {
      fprintf(stderr, "Test %zu failed: compilation error\n", i);
      goto failed;
    }
    // JIT test
    uint64_t* frame_bp = frame_stack + ONDA_VM_FRAME_STACK_SIZE;
    onda_comp_aarch64(cobj.code,
                      cobj.entry_pc,
                      cobj.size,
                      frame_bp,
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

    onda_code_obj_free(&cobj);
    printf("Test %zu passed.\n", i);
  }

  return 0;
failed:
  printf("Failed program:\n %s\n", tests[i].program);
  return 1;
}
