#include "onda_vm.h"

#include "onda_std.h"
#include "onda_util.h"

#include <stdio.h>
#include <string.h>

onda_vm_t* onda_vm_new() {
  onda_vm_t* vm = onda_calloc(1, sizeof(onda_vm_t));
  return vm;
}

int onda_vm_load_code(onda_vm_t* vm,
                      const uint8_t* code,
                      const size_t entry_pc,
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

int onda_vm_run(onda_vm_t* vm) {
  vm->pc = vm->entry_pc;
  vm->sp = 0;

  static void* dispatch_table[] = {
      [ONDA_OP_HALT] = &&op_halt,
      [ONDA_OP_ADD] = &&op_add,
      [ONDA_OP_SUB] = &&op_sub,
      [ONDA_OP_MUL] = &&op_mul,
      [ONDA_OP_DIV] = &&op_div,
      [ONDA_OP_MOD] = &&op_mod,
      [ONDA_OP_INC] = &&op_inc,
      [ONDA_OP_DEC] = &&op_dec,
      [ONDA_OP_AND] = &&op_and,
      [ONDA_OP_OR] = &&op_or,
      [ONDA_OP_NOT] = &&op_not,
      [ONDA_OP_EQ] = &&op_eq,
      [ONDA_OP_NEQ] = &&op_neq,
      [ONDA_OP_LT] = &&op_lt,
      [ONDA_OP_GT] = &&op_gt,
      [ONDA_OP_LTE] = &&op_lte,
      [ONDA_OP_GTE] = &&op_gte,
      [ONDA_OP_PUSH_CONST_U8] = &&op_push_const_u8,
      [ONDA_OP_PUSH_CONST_U32] = &&op_push_const_u32,
      [ONDA_OP_PUSH_CONST_U64] = &&op_push_const_u64,
      [ONDA_OP_SWAP] = &&op_swap,
      [ONDA_OP_DUP] = &&op_dup,
      [ONDA_OP_OVER] = &&op_over,
      [ONDA_OP_ROT] = &&op_rot,
      [ONDA_OP_DROP] = &&op_drop,
      [ONDA_OP_PRINT] = &&op_print,
      [ONDA_OP_PRINT_STR] = &&op_print_str,
  };

#define DISPATCH() goto* dispatch_table[vm->code[vm->pc++]];

  DISPATCH();

  uint64_t tmp;

// Arithmetic operations
op_add:
  vm->sp--;
  vm->stack[vm->sp - 1] += vm->stack[vm->sp];
  DISPATCH();
op_sub:
  vm->sp--;
  vm->stack[vm->sp - 1] -= vm->stack[vm->sp];
  DISPATCH();
op_mul:
  vm->sp--;
  vm->stack[vm->sp - 1] *= vm->stack[vm->sp];
  DISPATCH();
op_div:
  vm->sp--;
  vm->stack[vm->sp - 1] /= vm->stack[vm->sp];
  DISPATCH();
op_mod:
  vm->sp--;
  vm->stack[vm->sp - 1] %= vm->stack[vm->sp];
  DISPATCH();
op_inc:
  vm->stack[vm->sp - 1]++;
  DISPATCH();
op_dec:
  vm->stack[vm->sp - 1]--;
  DISPATCH();

// Logical operations
op_and:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] && vm->stack[vm->sp];
  DISPATCH();
op_or:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] || vm->stack[vm->sp];
  DISPATCH();
op_not:
  vm->stack[vm->sp - 1] = !vm->stack[vm->sp - 1];
  DISPATCH();

// Comparison operations
op_eq:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] == vm->stack[vm->sp];
  DISPATCH();
op_neq:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] != vm->stack[vm->sp];
  DISPATCH();
op_lt:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] < vm->stack[vm->sp];
  DISPATCH();
op_gt:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] > vm->stack[vm->sp];
  DISPATCH();
op_lte:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] <= vm->stack[vm->sp];
  DISPATCH();
op_gte:
  vm->sp--;
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 1] >= vm->stack[vm->sp];
  DISPATCH();

// Stack operations
op_push_const_u8:
  vm->stack[vm->sp++] = (int8_t)vm->code[vm->pc++];
  DISPATCH();
op_push_const_u32:
  memcpy(&tmp, &vm->code[vm->pc], 4);
  vm->pc += 4;
  vm->stack[vm->sp++] = (int32_t)tmp;
  DISPATCH();
op_push_const_u64:
  memcpy(&tmp, &vm->code[vm->pc], 8);
  vm->pc += 8;
  vm->stack[vm->sp++] = (int64_t)tmp;
  DISPATCH();
op_swap:
  tmp = vm->stack[vm->sp - 1];
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2];
  vm->stack[vm->sp - 2] = tmp;
  DISPATCH();
op_dup:
  vm->stack[vm->sp++] = vm->stack[vm->sp - 1];
  DISPATCH();
op_over:
  vm->stack[vm->sp++] = vm->stack[vm->sp - 2];
  DISPATCH();
op_rot:
  tmp = vm->stack[vm->sp - 1];
  vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2];
  vm->stack[vm->sp - 2] = vm->stack[vm->sp - 3];
  vm->stack[vm->sp - 3] = tmp;
  DISPATCH();
op_drop:
  vm->sp--;
  DISPATCH();
op_print:
  onda_print_u64((uint64_t)vm->stack[--vm->sp]);
  DISPATCH();
op_print_str : {
  onda_print_string((char*)vm->stack[--vm->sp]);
  DISPATCH();
}
op_halt:
  return 0;
}

void onda_vm_free(onda_vm_t* vm) {
  if (vm->code)
    onda_free(vm->code);
  onda_free(vm);
}
