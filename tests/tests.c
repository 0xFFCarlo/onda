#include "../src/onda_compiler.h"
#include "../src/onda_jit.h"
#include "../src/onda_std.h"
#include "../src/onda_vm.h"

#include <inttypes.h>
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
    //===================
    // Basic operations
    //===================
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

    //===================
    // Logic operators
    //===================
    // --- NOT (!): assume logical-not (0 -> 1, nonzero -> 0)
    {"0 not ret", 1, 1},
    {"5 not ret", 1, 0},

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
    // non-boolean operands should still normalize to 1/0
    {"2 4 and ret", 1, 1},
    {"0 7 and ret", 1, 0},

    // --- LOGICAL OR (or): assume nonzero=true, result 1/0
    {"0 0 or ret", 1, 0},
    {"1 0 or ret", 1, 1},
    // non-boolean operands should still normalize to 1/0
    {"2 4 or ret", 1, 1},
    {"0 9 or ret", 1, 1},

    //===================
    // Stack operations
    //===================
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

    //===================
    // If condition
    //===================
    {"if 1 then 2 end ret", 1, 2},
    {"if 0 then 2 end ret", 0, 0},
    {"if 2 3 > then 4 4 + else 5 5 + end ret", 1, 10},
    {"if 2 3 < then 4 4 + else 5 5 + end ret", 1, 8},
    {"if 1 then 3 else 4 end ret", 1, 3},
    {"if 0 then 3 else 4 end ret", 1, 4},

    //===================
    // While loop
    //===================
    {"10 while dup 2 > do -- end ret", 1, 2},
    {"5 while dup 0 > do 1 - end drop ret", 0, 0},
    {"0 while dup 10 < do ++ if dup 9 != then continue end 10 * end ret",
     1,
     90},
    // Single loop: sum 1..10 but skip 5 using continue
    {"0 10 while dup 0 > do "
     "if dup 5 == then -- continue end "
     "swap over + swap "
     "-- "
     "end drop ret",
     1,
     50},

    //===================
    // Words
    //===================
    {":square  dup * ; "
     ":main    5 square ; ",
     1,
     25},
    // Words recursion
    {":factorial  if dup 1 <= then drop 1 else dup 1 - factorial * end ; "
     ":main       5 factorial ; ",
     1,
     120},
    // Many words
    {":fun_c  3 ; "
     ":fun_b  2 fun_c + ; "
     ":fun_a  1 fun_b + ; "
     ":main   fun_a ; ",
     1,
     6},
    // Words with arguments
    {": dist ( a b ) a a * b b * + ;"
     ": main 3 4 dist ;",
     1,
     25},
    // Words with local temporaries and local assignment/access
    {": dist2 ( a b | aa bb ) "
     "a a * -> aa "
     "b b * -> bb "
     "aa bb + ; "
     ": main 3 4 dist2 ;",
     1,
     25},
    // Word with local temporaries and nested loop with continue statements
    {
        ": countdown ( n k | start_k i ) "
        "  0 -> i "
        "  k -> start_k "
        "  while n 0 > do "
        "    if n 3 == then n -- -> n continue end "
        "    start_k -> k "
        "    while k 0 > do "
        "      if k 2 == then k -- -> k continue end "
        "      i n + -> i "
        "      k -- -> k "
        "    end"
        "    n -- -> n "
        "  end i ; "
        ": main 5 5 countdown ;",
        1,
        48, // 5 * 4 + 4 * 4 + 2 * 4 + 1 * 4 = 48
    },
    //===================
    // Memory operations
    //===================
    {"16 malloc dup 10 swap ! @ ret", 1, 10},
    // Setting and getting multiple 64bit double-word values in memory
    {": test ( ptr ) 10 ptr ! 20 ptr 8 + ! ptr @ ptr 8 + @ ;"
     ": main 16 malloc test ;",
     2,
     20,
     10},
    // Setting and getting multiple 32bit words values in memory
    {": test ( ptr ) 10 ptr w! 20 ptr 4 + w! ptr w@ ptr 4 + w@ ;"
     ": main 16 malloc test ;",
     2,
     20,
     10},
    // Setting and getting multiple 16bit half-words values in memory
    {": test ( ptr ) 10 ptr h! 20 ptr 4 + h! ptr h@ ptr 4 + h@ ;"
     ": main 16 malloc test ;",
     2,
     20,
     10},
    // Setting and getting multiple bytes values in memory
    {": test ( ptr ) 10 ptr b! 20 ptr 4 + b! ptr b@ ptr 4 + b@ ;"
     ": main 16 malloc test ;",
     2,
     20,
     10},
    // Setting and getting multiple bytes values in memory,
    // with values exceeding byte range to test truncation
    {": test ( ptr ) 300 ptr b! 1024 ptr 4 + b! ptr b@ ptr 4 + b@ ;"
     ": main 16 malloc test ;",
     2,
     0,
     44},

    //===================
    // Large constants (exercise PUSH_CONST_U32 / PUSH_CONST_U64 paths)
    //===================
    // U32: value > 255
    {"65536 ret", 1, 65536},
    // U32: 0xFFFFFFFF (max 32-bit)
    {"4294967295 ret", 1, 4294967295LL},
    // U64: value > 0xFFFFFFFF
    {"4294967296 ret", 1, 4294967296LL},

    //===================
    // Signed comparisons with negative numbers
    //===================
    // -1 < 0 must be true (catches unsigned-vs-signed comparison bug)
    {"0 1 - 0 < ret", 1, 1},
    // -1 > 0 must be false
    {"0 1 - 0 > ret", 1, 0},
    // -1 <= -1 must be true
    {"0 1 - 0 1 - <= ret", 1, 1},

    //===================
    // Arithmetic edge cases
    //===================
    // MOD yielding zero
    {"6 3 % ret", 1, 0},
    // DIV of zero
    {"0 5 / ret", 1, 0},
    // Chained: (2 + 3) * 4 = 20
    {"2 3 + 4 * ret", 1, 20},
    // Signed division/modulo (negative operands)
    {"0 3 - 2 / ret", 1, -1},
    {"0 3 - 2 % ret", 1, -1},
    {"0 7 - 0 3 - / ret", 1, 2},
    {"0 7 - 0 3 - % ret", 1, -1},

};

int main() {
  onda_lexer_t lexer = {0};
  onda_env_t env;
  onda_code_obj_t cobj = {0};
  size_t i;
  onda_vm_t* vm = onda_vm_new();
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;
  int64_t frame_stack[ONDA_FRAME_STACK_SIZE];
  int64_t data_stack[ONDA_DATA_STACK_SIZE];
  onda_env_init(&env);
  onda_env_register_std(&env);

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
    if (onda_compile(&lexer, &env, &cobj) != 0) {
      fprintf(stderr, "Test %zu failed: compilation error\n", i);
      goto failed;
    }
    // onda_dict_free(&cobj.words_map);
    onda_vm_load_code(vm, cobj.code, cobj.entry_pc, cobj.size);
    vm->debug_mode = tc->debug_mode;
    onda_vm_run(vm);
    const size_t stack_size = vm->data_stack + ONDA_DATA_STACK_SIZE - vm->sp;
    if (stack_size != tc->stack_size) {
      fprintf(stderr,
              "Test %zu failed: expected stack size %zu, got %zu\n",
              i,
              tc->stack_size,
              stack_size);
      goto failed;
    }
    if (tc->stack_size > 0) {
      int64_t val = *vm->sp;
      if (val != tc->expected_result_a) {
        fprintf(stderr,
                "Test %zu failed: expected TOS %" PRId64 ", got %" PRId64 "\n",
                i,
                tc->expected_result_a,
                val);
        goto failed;
      }
    }
    if (tc->stack_size > 1) {
      int64_t val = *(vm->sp + 1);
      if (val != tc->expected_result_b) {
        fprintf(stderr,
                "Test %zu failed: expected TOS-1 %" PRId64 ", got %" PRId64 "\n",
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

#ifdef ONDA_CAN_JIT
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
    if (onda_compile(&lexer, &env, &cobj) != 0) {
      fprintf(stderr, "Test %zu failed: compilation error\n", i);
      goto failed;
    }
    // JIT test
    int64_t* frame_bp = frame_stack + ONDA_FRAME_STACK_SIZE;
    int64_t* data_sp = data_stack + ONDA_DATA_STACK_SIZE;
    onda_jit_compile(cobj.code,
                     cobj.entry_pc,
                     cobj.size,
                     data_sp,
                     frame_bp,
                     &machine_code,
                     &machine_code_size);
    uint64_t tos = onda_jit_run(machine_code, machine_code_size);
    if (tc->stack_size > 0) {
      if (tos != tc->expected_result_a) {
        fprintf(stderr,
                "Test %zu JIT failed: expected TOS %" PRId64 ", got %" PRIu64 "\n",
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
#endif // ONDA_CAN_JIT

  return 0;
failed:
  printf("Failed program:\n %s\n", tests[i].program);
  return 1;
}
