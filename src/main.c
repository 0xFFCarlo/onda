#include "onda_vm.h"
#include "onda_parser.h"
#include <stdio.h>


//static const char *src_code = "300 0 + 10 * dup + -1 swap * . \"hola\" .s halt\0";
static const char *src_code = "\"Hello world!\n\" .s halt\0";

int main(int argc, char *argv[]) {
  uint8_t codebuf[1024];
  size_t code_size = sizeof(codebuf);
  size_t entry_pc = 0;
  onda_parse(src_code, codebuf, &code_size, &entry_pc);
  for (int i = 0; i < code_size; i++) {
    printf("%02X ", codebuf[i]);
  }
  printf("\n");

  onda_vm_t *vm = onda_vm_new();
  onda_vm_load_code(vm, codebuf, 0, code_size);
  onda_vm_run(vm);
  printf("stack size: %lu\n", vm->sp);
  onda_vm_free(vm);
  return 0;
}
