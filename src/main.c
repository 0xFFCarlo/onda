#include "onda_compiler.h"
#include "onda_config.h"
#include "onda_jit.h"
#include "onda_std.h"
#include "onda_vm.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

#define CODE_BUF_SIZE 1024

int main(int argc, char* argv[]) {
  struct timespec start, end;
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
    return 1;
  }
  onda_lexer_t lexer = {0};
  onda_code_obj_t cobj = {0};
  onda_env_t env;
  onda_env_init(&env);
  onda_env_register_std(&env);
  onda_code_obj_init(&cobj, CODE_BUF_SIZE);
  if (onda_compile_file(argv[1], &lexer, &env, &cobj) != 0) {
    fprintf(stderr, "Failed to parse source file: %s\n", argv[1]);
    return 1;
  }
  onda_vm_print_bytecode(cobj.code, cobj.size);

#ifdef ONDA_CAN_JIT
  // Compile to machine code
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;
  int64_t frame_stack[ONDA_FRAME_STACK_SIZE];
  int64_t data_stack[ONDA_DATA_STACK_SIZE];
  int64_t* frame_bp = frame_stack + ONDA_FRAME_STACK_SIZE;
  int64_t* data_sp = data_stack + ONDA_DATA_STACK_SIZE;
  onda_jit_compile(cobj.code,
                   cobj.entry_pc,
                   cobj.size,
                   data_sp,
                   frame_bp,
                   &machine_code,
                   &machine_code_size,
                   &env.native_registry);

  // Execute program in JIT
  clock_gettime(CLOCK_MONOTONIC, &start);
  uint64_t tos = onda_jit_run(machine_code, machine_code_size);
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("Execution result (TOS): %" PRIu64 "\n", tos);
  printf("Execution time: %.3f ms\n",
         (end.tv_sec - start.tv_sec) * 1000.0 +
             (end.tv_nsec - start.tv_nsec) / 1000000.0);
#else
  // Execute program in VM, starting from entry_pc
  onda_vm_t* vm = onda_vm_new();
  onda_vm_load_code(vm, cobj.code, cobj.entry_pc, cobj.size);
  vm->env = &env;
  printf("Executing with VM:\n");
  vm->debug_mode = true;
  clock_gettime(CLOCK_MONOTONIC, &start);
  onda_vm_run(vm);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (vm->sp < vm->data_stack + ONDA_DATA_STACK_SIZE) {
    uint64_t tos = *vm->sp;
    printf("Execution result (TOS): %" PRIu64 "\n", tos);
  } else {
    printf("Stack is empty after execution.\n");
  }
  printf("Execution time: %.3f ms\n",
         (end.tv_sec - start.tv_sec) * 1000.0 +
             (end.tv_nsec - start.tv_nsec) / 1000000.0);
  printf("\n");
  onda_vm_free(vm);
#endif // ONDA_CAN_JIT

  onda_env_free(&env);
  onda_code_obj_free(&cobj);
  return 0;
}
