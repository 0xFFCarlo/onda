#include "onda_comp_aarch64.h"
#include "onda_compiler.h"
#include "onda_jit.h"
#include "onda_vm.h"

#include <stdio.h>
#include <time.h>

#define CODE_BUF_SIZE 1024

static int read_file(const char* filename,
                     uint8_t* buffer,
                     size_t buffer_size,
                     size_t* out_size) {
  FILE* f = fopen(filename, "rb");
  if (!f) {
    fprintf(stderr, "Failed to open file: %s\n", filename);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (file_size < 0 || (size_t)file_size > buffer_size) {
    fprintf(stderr,
            "File size (%ld bytes) exceeds buffer capacity (%zu bytes)\n",
            file_size,
            buffer_size);
    fclose(f);
    return -1;
  }
  size_t read_size = fread(buffer, 1, file_size, f);
  fclose(f);
  if (read_size != (size_t)file_size) {
    fprintf(stderr,
            "Failed to read entire file: expected %ld bytes, got %zu bytes\n",
            file_size,
            read_size);
    return -1;
  }
  *out_size = read_size;
  buffer[read_size] = '\0'; // null-terminate for lexer
  return 0;
}

int main(int argc, char* argv[]) {
  struct timespec start, end;
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <source_file>\n", argv[0]);
    return 1;
  }
  uint8_t filebuf[2048];
  size_t file_size = 0;
  if (read_file(argv[1], filebuf, sizeof(filebuf), &file_size) != 0)
    return 1;
  onda_lexer_t lexer = {
      .src = (const char*)filebuf,
      .pos = 0,
      .line = 0,
      .column = 0,
  };
  onda_code_obj_t cobj;
  onda_code_obj_init(&cobj, CODE_BUF_SIZE);
  if (onda_compile(&lexer, &cobj) != 0) {
    fprintf(stderr, "Failed to parse source file: %s\n", argv[1]);
    return 1;
  }

  // Execute program in VM, starting from entry_pc
  onda_vm_t* vm = onda_vm_new();
  vm->debug_mode = true;
  onda_vm_load_code(vm, cobj.code, cobj.entry_pc, cobj.size);
  printf("Executing with VM:\n");
  clock_gettime(CLOCK_MONOTONIC, &start);
  onda_vm_run(vm);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (vm->sp > 0) {
    uint64_t tos = vm->data_stack[vm->sp - 1];
    printf("VM execution result (TOS): %llu\n", tos);
  } else {
    printf("VM stack is empty after execution.\n");
  }
  printf("VM execution time: %.3f ms\n",
         (end.tv_sec - start.tv_sec) * 1000.0 +
             (end.tv_nsec - start.tv_nsec) / 1000000.0);
  printf("\n");

#if defined(__aarch64__)
  // Compile to machine code (AArch64)
  uint8_t* machine_code = NULL;
  size_t machine_code_size = 0;
  uint64_t frame_stack[ONDA_VM_FRAME_STACK_SIZE];
  uint64_t* frame_bp = frame_stack + ONDA_VM_FRAME_STACK_SIZE;
  onda_comp_aarch64(cobj.code,
                    cobj.entry_pc,
                    cobj.size,
                    frame_bp,
                    &machine_code,
                    &machine_code_size);

  // Execute program in JIT
  printf("Executing with JIT:\n");
  clock_gettime(CLOCK_MONOTONIC, &start);
  uint64_t tos = onda_jit_run(machine_code, machine_code_size);
  clock_gettime(CLOCK_MONOTONIC, &end);
  printf("JIT execution result (TOS): %llu\n", tos);
  printf("JIT execution time: %.3f ms\n",
         (end.tv_sec - start.tv_sec) * 1000.0 +
             (end.tv_nsec - start.tv_nsec) / 1000000.0);
#endif // __aarch64__

  onda_vm_free(vm);
  return 0;
}
