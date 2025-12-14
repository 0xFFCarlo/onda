#include "onda_vm.h"
#include "onda_util.h"
#include <string.h>

onda_vm_t *onda_vm_new() {
  onda_vm_t *vm = onda_calloc(1, sizeof(onda_vm_t));
  return vm;
}

int onda_vm_load_code(onda_vm_t *vm, const uint8_t *code, const size_t entry_pc,
                      const size_t code_size) {
  if (vm->code)
    onda_free(vm->code);
  vm->code = onda_malloc(code_size);
  memcpy(vm->code, code, code_size);
  vm->code_size = code_size;
  vm->entry_pc = entry_pc;
  vm->pc = 0;
  return 0;
}

int onda_vm_run(onda_vm_t *vm) {
  vm->pc = vm->entry_pc;
  vm->sp = 0;

  static void *dispatch_table[] = {
      [ONDA_OP_HALT] = &&op_halt, [ONDA_OP_ADD] = &&op_add,
      [ONDA_OP_SUB] = &&op_sub,   [ONDA_OP_MUL] = &&op_mul,
      [ONDA_OP_DIV] = &&op_div,   [ONDA_OP_PUSH_CONST_U8] = &&op_push_const_u8,
      [ONDA_OP_PUSH_CONST_U32] = &&op_push_const_u32,
      [ONDA_OP_PUSH_CONST_U64] = &&op_push_const_u64,
  };

#define DISPATCH() goto *dispatch_table[vm->code[vm->pc++]];

  DISPATCH();

  uint64_t tmp;

op_push_const_u8:
  vm->stack[vm->sp++] = vm->code[vm->pc++];
  DISPATCH();
op_push_const_u32:
  tmp = vm->code[vm->pc++];
  tmp |= (uint32_t)vm->code[vm->pc++] << 8;
  tmp |= (uint32_t)vm->code[vm->pc++] << 16;
  tmp |= (uint32_t)vm->code[vm->pc++] << 24;
  vm->stack[vm->sp++] = tmp;
  DISPATCH();
op_push_const_u64:
  tmp = vm->code[vm->pc++];
  tmp |= (uint64_t)vm->code[vm->pc++] << 8;
  tmp |= (uint64_t)vm->code[vm->pc++] << 16;
  tmp |= (uint64_t)vm->code[vm->pc++] << 24;
  tmp |= (uint64_t)vm->code[vm->pc++] << 32;
  tmp |= (uint64_t)vm->code[vm->pc++] << 40;
  tmp |= (uint64_t)vm->code[vm->pc++] << 48;
  tmp |= (uint64_t)vm->code[vm->pc++] << 56;
  vm->stack[vm->sp++] = tmp;
  DISPATCH();
op_add:
  vm->stack[vm->sp - 2] += vm->stack[vm->sp - 1];
  vm->sp--;
  DISPATCH();
op_sub:
  vm->stack[vm->sp - 2] -= vm->stack[vm->sp - 1];
  vm->sp--;
  DISPATCH();
op_mul:
  vm->stack[vm->sp - 2] *= vm->stack[vm->sp - 1];
  vm->sp--;
  DISPATCH();
op_div:
  vm->stack[vm->sp - 2] /= vm->stack[vm->sp - 1];
  vm->sp--;
  DISPATCH();
op_halt:
  return 0;
}

void onda_vm_free(onda_vm_t *vm) {
  if (vm->code)
    onda_free(vm->code);
  onda_free(vm);
}
