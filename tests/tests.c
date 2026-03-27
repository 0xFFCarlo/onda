#include "../src/onda_compiler.h"
#include "../src/onda_jit.h"
#include "../src/onda_std.h"
#include "../src/onda_vm.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct test_case_t {
  const char* program;
  size_t stack_size;
  int64_t expected_result_a;
  int64_t expected_result_b;
  bool debug_mode;
} test_case_t;

#define GET_TEST_CASE_MACRO(_1, _2, _3, _4, _5, NAME, ...) NAME
#define TEST(...)                                                             \
  GET_TEST_CASE_MACRO(__VA_ARGS__, TEST5, TEST4, TEST3)(__VA_ARGS__)
#define TEST3(program, stack_size, expected_result_a)                         \
  {                                                                           \
      (program), (stack_size), (expected_result_a), 0, false,                 \
  }
#define TEST4(program, stack_size, expected_result_a, expected_result_b)       \
  {                                                                           \
      (program),                                                              \
      (stack_size),                                                           \
      (expected_result_a),                                                    \
      (expected_result_b),                                                    \
      false,                                                                  \
  }
#define TEST5(program,                                                         \
              stack_size,                                                      \
              expected_result_a,                                               \
              expected_result_b,                                               \
              debug_mode)                                                      \
  {                                                                            \
      (program),                                                               \
      (stack_size),                                                            \
      (expected_result_a),                                                     \
      (expected_result_b),                                                     \
      (debug_mode),                                                            \
  }

static const test_case_t tests[] = {
    //===================
    // Basic operations
    //===================
    // --- RET (empty stack)
    TEST("ret", 0, 0x0),

    // --- PUSH CONSTANTS
    TEST("10 ret", 1, 10),

    // --- PUSH MULTIPLE CONSTANTS
    TEST("10 20 ret", 2, 20, 10),

    // --- ADD (+): minimal extra coverage (order shouldn't matter)
    TEST("2 4 + ret", 1, 6),

    // --- SUB (-): cover negative result explicitly
    TEST("4 1 - ret", 1, 3),

    // --- SUB (-): negative result
    TEST("1 4 - ret", 1, -3),

    // --- INC (++)
    TEST("41 ++ ret", 1, 42),

    // --- DEC (--)
    TEST("43 -- ret", 1, 42),

    // --- MULTIPLY (*)
    TEST("6 7 * ret", 1, 42),

    // --- DIVIDE (/): integer division assumed
    TEST("7 2 / ret", 1, 3),

    // --- MODULO (%)
    TEST("7 2 % ret", 1, 1),
    // --- SHIFT LEFT (<<)
    TEST("3 2 << ret", 1, 12),
    // --- SHIFT RIGHT (>>)
    TEST("8 2 >> ret", 1, 2),
    // arithmetic right shift on negative numbers
    TEST("0 8 - 2 >> ret", 1, -2),
    // --- BITWISE AND (&)
    TEST("6 3 & ret", 1, 2),
    // --- BITWISE OR (|)
    TEST("6 3 | ret", 1, 7),
    // --- BITWISE XOR (^)
    TEST("6 3 ^ ret", 1, 5),
    // --- BITWISE NOT (~)
    TEST("0 ~ ret", 1, -1),

    //===================
    // Logic operators
    //===================
    // --- NOT (!): assume logical-not (0 -> 1, nonzero -> 0)
    TEST("0 not ret", 1, 1),
    TEST("5 not ret", 1, 0),

    // --- EQUALITY (==)
    TEST("42 42 == ret", 1, 1),
    TEST("42 7 == ret", 1, 0),

    // --- NOT EQUAL (!=)
    TEST("42 7 != ret", 1, 1),
    TEST("42 42 != ret", 1, 0),

    // --- LESS (<)
    TEST("1 2 < ret", 1, 1),
    TEST("2 1 < ret", 1, 0),

    // --- LESS EQUAL (<=)
    TEST("2 2 <= ret", 1, 1),
    TEST("3 2 <= ret", 1, 0),

    // --- GREATER (>)
    TEST("2 1 > ret", 1, 1),
    TEST("1 2 > ret", 1, 0),

    // --- GREATER EQUAL (>=)
    TEST("2 2 >= ret", 1, 1),
    TEST("1 2 >= ret", 1, 0),

    // --- LOGICAL AND (and): assume nonzero=true, result 1/0
    TEST("1 1 and ret", 1, 1),
    TEST("1 0 and ret", 1, 0),
    // non-boolean operands should still normalize to 1/0
    TEST("2 4 and ret", 1, 1),
    TEST("0 7 and ret", 1, 0),

    // --- LOGICAL OR (or): assume nonzero=true, result 1/0
    TEST("0 0 or ret", 1, 0),
    TEST("1 0 or ret", 1, 1),
    // non-boolean operands should still normalize to 1/0
    TEST("2 4 or ret", 1, 1),
    TEST("0 9 or ret", 1, 1),

    //===================
    // Stack operations
    //===================
    // drop: (a -- )
    TEST("10 drop ret", 0, 0),

    // dup: (a -- a a)
    TEST("7 dup ret", 2, 7, 7),

    // swap: (a b -- b a)
    TEST("10 20 swap ret", 2, 10, 20),

    // over: (a b -- a b a)  (copies 2nd item to top)
    TEST("10 20 over ret", 3, 10, 20),

    // rot: (a b c -- b c a)
    TEST("1 2 3 rot ret", 3, 2, 1),
    // depth: ( -- n)
    TEST("10 20 depth ret", 3, 2, 20),
    // .stack: ( -- ) prints whole stack and keeps it unchanged
    TEST("10 20 .stack ret", 2, 20, 10),

    //===================
    // If condition
    //===================
    TEST("if 1 then 2 end ret", 1, 2),
    TEST("if 0 then 2 end ret", 0, 0),
    TEST("if 2 3 > then 4 4 + else 5 5 + end ret", 1, 10),
    TEST("if 2 3 < then 4 4 + else 5 5 + end ret", 1, 8),
    TEST("if 1 then 3 else 4 end ret", 1, 3),
    TEST("if 0 then 3 else 4 end ret", 1, 4),

    //===================
    // While loop
    //===================
    TEST("10 while dup 2 > do -- end ret", 1, 2),
    TEST("5 while dup 0 > do 1 - end drop ret", 0, 0),
    TEST("0 while dup 10 < do ++ if dup 9 != then continue end 10 * end ret",
         1,
         90),
    // Computed jump via label address (backward jump loop).
    TEST(": main ( | i ) 0 -> i label loop i 1 + -> i if i 5 < then loop jump end i ;",
         1,
         5),
    // Single loop: sum 1..10 but skip 5 using continue
    TEST("0 10 while dup 0 > do "
         "if dup 5 == then -- continue end "
         "swap over + swap "
         "-- "
         "end drop ret",
         1,
         50),

    //===================
    // Words
    //===================
    TEST(":square  dup * ; "
         ":main    5 square ; ",
         1,
         25),
    // Words recursion
    TEST(":factorial  if dup 1 <= then drop 1 else dup 1 - factorial * end ; "
         ":main       5 factorial ; ",
         1,
         120),
    // Many words
    TEST(":fun_c  3 ; "
         ":fun_b  2 fun_c + ; "
         ":fun_a  1 fun_b + ; "
         ":main   fun_a ; ",
         1,
         6),
    // Words with arguments
    TEST(": dist ( a b ) a a * b b * + ;"
         ": main 3 4 dist ;",
         1,
         25),
    // Words with local temporaries and local assignment/access
    TEST(": dist2 ( a b | aa bb ) "
         "a a * -> aa "
         "b b * -> bb "
         "aa bb + ; "
         ": main 3 4 dist2 ;",
         1,
         25),
    // Main word with local temporaries
    TEST(": main ( | tmp ) 10 -> tmp tmp ;", 1, 10),
    // Word with local temporaries and nested loop with continue statements
    TEST(": countdown ( n k | start_k i ) "
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
         48), // 5 * 4 + 4 * 4 + 2 * 4 + 1 * 4 = 48
    //===================
    // Memory operations
    //===================
    TEST("16 malloc dup 10 swap ! @ ret", 1, 10),
    // Setting and getting multiple 64bit double-word values in memory
    TEST(": test ( ptr ) 10 ptr ! 20 ptr 8 + ! ptr @ ptr 8 + @ ;"
         ": main 16 malloc test ;",
         2,
         20,
         10),
    // Setting and getting multiple 32bit words values in memory
    TEST(": test ( ptr ) 10 ptr w! 20 ptr 4 + w! ptr w@ ptr 4 + w@ ;"
         ": main 16 malloc test ;",
         2,
         20,
         10),
    // Setting and getting multiple 16bit half-words values in memory
    TEST(": test ( ptr ) 10 ptr h! 20 ptr 4 + h! ptr h@ ptr 4 + h@ ;"
         ": main 16 malloc test ;",
         2,
         20,
         10),
    // Setting and getting multiple bytes values in memory
    TEST(": test ( ptr ) 10 ptr b! 20 ptr 4 + b! ptr b@ ptr 4 + b@ ;"
         ": main 16 malloc test ;",
         2,
         20,
         10),
    // Setting and getting multiple bytes values in memory,
    // with values exceeding byte range to test truncation
    TEST(": test ( ptr ) 300 ptr b! 1024 ptr 4 + b! ptr b@ ptr 4 + b@ ;"
         ": main 16 malloc test ;",
         2,
         0,
         44),
    //===================
    // C stdlib wrappers
    //===================
    // Stack-order regression checks (C-order arguments, last arg on TOS)
    TEST("\"a\" \"b\" strcmp 0 < ret", 1, 1),
    TEST("\"ab\" \"aa\" 2 memcmp 0 > ret", 1, 1),
    TEST("\"ab\" \"aa\" 2 strncmp 0 > ret", 1, 1),
    TEST(":cpy1 ( | dst src ) 1 16 calloc -> dst 1 16 calloc -> src "
         "65 src b! "
         "dst src 1 memcpy "
         "dst b@ ; "
         ":main cpy1 ;",
         1,
         65),

    // String search helpers
    TEST("\"hello\" dup 101 strchr swap 1 + == ret", 1, 1),
    TEST("\"banana\" dup \"nan\" strstr swap 2 + == ret", 1, 1),

    // Number parsing helpers
    TEST("\"123\" atoi ret", 1, 123),
    TEST("\"2a\" 16 strtol ret", 1, 42),
    TEST("\"2a\" 16 strtoul ret", 1, 42),
    // assert(cond,msg) consumes args and continues when cond is true
    TEST("1 \"assert should not fail\" assert 7 ret", 1, 7),

    //===================
    // Large constants (exercise PUSH_CONST_U32 / PUSH_CONST_U64 paths)
    //===================
    // U32: value > 255
    TEST("65536 ret", 1, 65536),
    // U32: 0xFFFFFFFF (max 32-bit)
    TEST("4294967295 ret", 1, 4294967295LL),
    // U64: value > 0xFFFFFFFF
    TEST("4294967296 ret", 1, 4294967296LL),
    // Hex literals
    TEST("0x2A ret", 1, 42),
    TEST("-0x2A ret", 1, -42),
    // Binary literals
    TEST("0b101010 ret", 1, 42),
    TEST("-0b101010 ret", 1, -42),

    //===================
    // Signed comparisons with negative numbers
    //===================
    // -1 < 0 must be true (catches unsigned-vs-signed comparison bug)
    TEST("0 1 - 0 < ret", 1, 1),
    // -1 > 0 must be false
    TEST("0 1 - 0 > ret", 1, 0),
    // -1 <= -1 must be true
    TEST("0 1 - 0 1 - <= ret", 1, 1),

    //===================
    // Arithmetic edge cases
    //===================
    // MOD yielding zero
    TEST("6 3 % ret", 1, 0),
    // DIV of zero
    TEST("0 5 / ret", 1, 0),
    // Chained: (2 + 3) * 4 = 20
    TEST("2 3 + 4 * ret", 1, 20),
    // Immediate arithmetic fusion paths
    TEST("5 3 * 1 + ret", 1, 16),
    // Fused local inc/dec must preserve caller TOS
    TEST(": f ( | x ) 1 -> x x -- -> x x ; : main 7 f + ;", 1, 7),
    // Alias word inlines body at call site
    TEST("::inc1 1 + ; :main 41 inc1 ;", 1, 42),
    // Alias can be reused and expanded multiple times
    TEST("::twice dup + ; :main 10 twice twice ;", 1, 40),
    // Exported symbols are valid definitions
    TEST("export :inc 1 + ; export ::twice dup + ; :main 20 inc twice ;", 1, 42),
    // Signed division/modulo (negative operands)
    TEST("0 3 - 2 / ret", 1, -1),
    TEST("0 3 - 2 % ret", 1, -1),
    TEST("0 7 - 0 3 - / ret", 1, 2),
    TEST("0 7 - 0 3 - % ret", 1, -1),

};

static const char* compile_fail_tests[] = {
    "::bad ( a ) a ; :main 1 bad ;",
    "::bad 1 -> x ; :main bad ;",
    ": foo 1 ; : main ( foo ) foo ;",
    ": dup 1 ; : main dup ;",
    ": foo|bar 1 ; : main foo|bar ;",
    ": main ( a: b ) a: ;",
    ": main ( a ) a ;",
    ": main 1",
    "export 1",
    "export",
};

int main(void) {
  onda_lexer_t lexer = {0};
  onda_env_t env;
  onda_code_obj_t cobj = {0};
  size_t i;
  onda_vm_t* vm = onda_vm_new();
#ifdef ONDA_CAN_JIT
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;
#endif
  onda_env_init(&env);
  onda_env_register_std(&env);
  vm->env = &env;

  // Run tests using VM
  printf("Testing with VM:\n");
  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    onda_code_obj_init(&cobj, ONDA_CODE_BUF_SIZE);
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
    onda_vm_load_code(vm,
                      cobj.code,
                      cobj.entry_pc,
                      cobj.size,
                      cobj.const_pool,
                      cobj.const_pool_size);
    vm->debug_mode = tc->debug_mode;
    onda_vm_run(vm);
    const size_t stack_size =
        vm->runtime.data_stack + ONDA_DATA_STACK_SIZE - vm->sp;
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
                "Test %zu failed: expected TOS-1 %" PRId64 ", got %" PRId64
                "\n",
                i,
                tc->expected_result_b,
                val);
        goto failed;
      }
    }

    onda_code_obj_free(&cobj);
    printf("Test %zu passed.\n", i);
  }

  printf("\nTesting compile failures:\n");
  for (i = 0; i < sizeof(compile_fail_tests) / sizeof(compile_fail_tests[0]);
       i++) {
    onda_code_obj_init(&cobj, ONDA_CODE_BUF_SIZE);
    cobj.size = 0;
    cobj.entry_pc = 0;
    lexer.src = compile_fail_tests[i];
    lexer.column = 0;
    lexer.line = 0;
    lexer.pos = 0;
    if (onda_compile(&lexer, &env, &cobj) == 0) {
      fprintf(stderr,
              "Compile-fail test %zu failed: expected compilation error\n",
              i);
      goto failed;
    }
    onda_code_obj_free(&cobj);
    printf("Compile-fail test %zu passed.\n", i);
  }

  onda_vm_free(vm);

#ifdef ONDA_CAN_JIT
  // Run tests using JIT only (without VM execution)
  printf("\nTesting with JIT:\n");
  for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    onda_code_obj_init(&cobj, ONDA_CODE_BUF_SIZE);
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
    onda_runtime_t rt = {
        .code = cobj.code,
        .code_size = cobj.size,
        .entry_pc = cobj.entry_pc,
        .const_pool = cobj.const_pool,
        .const_pool_size = cobj.const_pool_size,
        .native_registry = &env.native_registry,
    };
    onda_runtime_reset(&rt);
    onda_jit_compile(&rt, &machine_code, &machine_code_size);
    int64_t tos = (int64_t)onda_jit_run(machine_code, machine_code_size);
    if (tc->stack_size > 0) {
      if (tos != tc->expected_result_a) {
        fprintf(stderr,
                "Test %zu JIT failed: expected TOS %" PRId64 ", got %" PRId64
                "\n",
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
