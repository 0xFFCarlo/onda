#include "onda_vm.h"
#include <stdio.h>

static const uint8_t code[] = {
    ONDA_OP_PUSH_CONST_U8, 6,  //
    ONDA_OP_PUSH_CONST_U8, 4,  //
    ONDA_OP_ADD,               //
    ONDA_OP_PUSH_CONST_U8, 10, //
    ONDA_OP_MUL,               //
    ONDA_OP_HALT,              //
};

int main(int argc, char *argv[]) {
  printf("Hello world\n");
  onda_vm_t *vm = onda_vm_new();
  onda_vm_load_code(vm, code, 0, sizeof(code));
  onda_vm_run(vm);
  printf("%lu\n", vm->stack[vm->sp - 1]);
  printf("stack size: %lu\n", vm->sp);
  onda_vm_free(vm);
  return 0;
}
