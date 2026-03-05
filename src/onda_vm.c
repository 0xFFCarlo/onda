#include "onda_vm.h"

#include "onda_std.h"
#include "onda_util.h"

#include <stdio.h>
#include <stdlib.h>
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
  vm->sp = 0;
  return 0;
}

static const char* opcode_to_str[ONDA_OP_COUNT] = {
    [ONDA_OP_RET] = "RET",
    [ONDA_OP_ADD] = "ADD",
    [ONDA_OP_SUB] = "SUB",
    [ONDA_OP_MUL] = "MUL",
    [ONDA_OP_DIV] = "DIV",
    [ONDA_OP_MOD] = "MOD",
    [ONDA_OP_INC] = "INC",
    [ONDA_OP_DEC] = "DEC",
    [ONDA_OP_AND] = "AND",
    [ONDA_OP_OR] = "OR",
    [ONDA_OP_NOT] = "NOT",
    [ONDA_OP_EQ] = "EQ",
    [ONDA_OP_NEQ] = "NEQ",
    [ONDA_OP_LT] = "LT",
    [ONDA_OP_GT] = "GT",
    [ONDA_OP_LTE] = "LTE",
    [ONDA_OP_GTE] = "GTE",
    [ONDA_OP_PUSH_CONST_U8] = "PUSH_CONST_U8",
    [ONDA_OP_PUSH_CONST_U32] = "PUSH_CONST_U32",
    [ONDA_OP_PUSH_CONST_U64] = "PUSH_CONST_U64",
    [ONDA_OP_PUSH_LOCAL] = "PUSH_FROM_LOCAL",
    [ONDA_OP_STORE_LOCAL] = "STORE_TO_LOCAL",
    [ONDA_OP_PUSH_FROM_ADDR_B] = "PUSH_FROM_ADDR_B",
    [ONDA_OP_STORE_TO_ADDR_B] = "STORE_TO_ADDR_B",
    [ONDA_OP_PUSH_FROM_ADDR_HW] = "PUSH_FROM_ADDR_Hw",
    [ONDA_OP_STORE_TO_ADDR_HW] = "STORE_TO_ADDR_HW",
    [ONDA_OP_PUSH_FROM_ADDR_W] = "PUSH_FROM_ADDR_W",
    [ONDA_OP_STORE_TO_ADDR_W] = "STORE_TO_ADDR_W",
    [ONDA_OP_PUSH_FROM_ADDR_DW] = "PUSH_FROM_ADDR_DW",
    [ONDA_OP_STORE_TO_ADDR_DW] = "STORE_TO_ADDR_DW",
    [ONDA_OP_SWAP] = "SWAP",
    [ONDA_OP_DUP] = "DUP",
    [ONDA_OP_OVER] = "OVER",
    [ONDA_OP_ROT] = "ROT",
    [ONDA_OP_DROP] = "DROP",
    [ONDA_OP_JUMP] = "JUMP",
    [ONDA_OP_JUMP_IF_FALSE] = "JUMP_IF_FALSE",
    [ONDA_OP_CALL] = "CALL",
    [ONDA_OP_CALL_NATIVE] = "CALL_NATIVE",
    [ONDA_OP_PRINT] = "PRINT",
    [ONDA_OP_PRINT_STR] = "PRINT_STR",
    [ONDA_OP_MALLOC] = "MALLOC",
    [ONDA_OP_FREE] = "FREE",
};

static const uint8_t opcode_args_byte[ONDA_OP_COUNT] = {
    [ONDA_OP_PUSH_CONST_U8] = sizeof(uint8_t),
    [ONDA_OP_PUSH_CONST_U32] = sizeof(uint32_t),
    [ONDA_OP_PUSH_CONST_U64] = sizeof(uint64_t),
    [ONDA_OP_PUSH_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_STORE_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_JUMP] = sizeof(int16_t),
    [ONDA_OP_JUMP_IF_FALSE] = sizeof(int16_t),
    [ONDA_OP_CALL] = sizeof(int32_t) + 2,
    [ONDA_OP_CALL_NATIVE] = sizeof(uint64_t),
};

void onda_vm_print_bytecode(const uint8_t* code, size_t code_size) {
  for (size_t i = 0; i < code_size; i++) {
    uint8_t opcode = code[i];
    printf("%04zu: %s", i, opcode_to_str[opcode]);
    size_t arg_bytes = opcode_args_byte[opcode];
    for (size_t j = 0; j < arg_bytes; j++) {
      printf(" %02X", code[i + 1 + j]);
    }
    printf("\n");
    i += arg_bytes;
  }
}

void debug_step(onda_vm_t* vm) {
  uint8_t opcode = vm->code[vm->pc];
  printf("PC: %04zu, SP: %04zu, FBP: %04zu, Opcode: %s\n",
         vm->pc,
         vm->sp,
         (vm->frame_stack + ONDA_VM_FRAME_STACK_SIZE) - vm->frame_bp,
         opcode_to_str[opcode]);
  printf("DS: {");
  for (size_t i = 0; i < vm->sp; i++) {
    printf("%lld", vm->data_stack[i]);
    if (i < vm->sp - 1)
      printf(", ");
  }
  printf("}\n");
  printf("FS: {");
  size_t frame_size;
  if (vm->frame_bp == vm->frame_stack + ONDA_VM_FRAME_STACK_SIZE) {
    frame_size = 2;
  } else {
    frame_size = (uint64_t*)vm->frame_bp[1] - vm->frame_bp;
  }
  for (size_t i = 0; i < frame_size; i++) {
    printf("%lld", vm->frame_bp[i]);
    if (i < frame_size - 1)
      printf(", ");
  }
  printf("}\n\n");
}

int onda_vm_run(onda_vm_t* vm) {
  vm->pc = vm->entry_pc;
  vm->frame_bp = vm->frame_stack + ONDA_VM_FRAME_STACK_SIZE;
  vm->sp = 0;

  static const void* dispatch_table[] = {
      [ONDA_OP_RET] = &&op_ret,
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
      [ONDA_OP_PUSH_LOCAL] = &&op_push_from_local,
      [ONDA_OP_STORE_LOCAL] = &&op_store_to_local,
      [ONDA_OP_PUSH_FROM_ADDR_B] = &&op_push_from_addr_b,
      [ONDA_OP_STORE_TO_ADDR_B] = &&op_store_to_addr_b,
      [ONDA_OP_PUSH_FROM_ADDR_HW] = &&op_push_from_addr_hw,
      [ONDA_OP_STORE_TO_ADDR_HW] = &&op_store_to_addr_hw,
      [ONDA_OP_PUSH_FROM_ADDR_W] = &&op_push_from_addr_w,
      [ONDA_OP_STORE_TO_ADDR_W] = &&op_store_to_addr_w,
      [ONDA_OP_PUSH_FROM_ADDR_DW] = &&op_push_from_addr_dw,
      [ONDA_OP_STORE_TO_ADDR_DW] = &&op_store_to_addr_dw,
      [ONDA_OP_SWAP] = &&op_swap,
      [ONDA_OP_DUP] = &&op_dup,
      [ONDA_OP_OVER] = &&op_over,
      [ONDA_OP_ROT] = &&op_rot,
      [ONDA_OP_DROP] = &&op_drop,
      [ONDA_OP_JUMP] = &&op_jmp,
      [ONDA_OP_JUMP_IF_FALSE] = &&op_jmp_if_false,
      [ONDA_OP_CALL] = &&op_call,
      [ONDA_OP_CALL_NATIVE] = &&op_call_native,
      [ONDA_OP_PRINT] = &&op_print,
      [ONDA_OP_PRINT_STR] = &&op_print_str,
      [ONDA_OP_MALLOC] = &&op_malloc,
      [ONDA_OP_FREE] = &&op_free,
  };

#ifdef ONDA_VM_DEBUG_MODE
#define DISPATCH()                                                             \
  if (vm->debug_mode)                                                          \
    debug_step(vm);                                                            \
  goto* dispatch_table[vm->code[vm->pc++]];
#else
#define DISPATCH() goto* dispatch_table[vm->code[vm->pc++]];
#endif

  DISPATCH();

  uint64_t tmp;
  int16_t jmp_offset;

// Arithmetic operations
op_add:
  vm->sp--;
  vm->data_stack[vm->sp - 1] += vm->data_stack[vm->sp];
  DISPATCH();
op_sub:
  vm->sp--;
  vm->data_stack[vm->sp - 1] -= vm->data_stack[vm->sp];
  DISPATCH();
op_mul:
  vm->sp--;
  vm->data_stack[vm->sp - 1] *= vm->data_stack[vm->sp];
  DISPATCH();
op_div:
  vm->sp--;
  vm->data_stack[vm->sp - 1] /= vm->data_stack[vm->sp];
  DISPATCH();
op_mod:
  vm->sp--;
  vm->data_stack[vm->sp - 1] %= vm->data_stack[vm->sp];
  DISPATCH();
op_inc:
  vm->data_stack[vm->sp - 1]++;
  DISPATCH();
op_dec:
  vm->data_stack[vm->sp - 1]--;
  DISPATCH();

// Logical operations
op_and:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] && vm->data_stack[vm->sp];
  DISPATCH();
op_or:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] || vm->data_stack[vm->sp];
  DISPATCH();
op_not:
  vm->data_stack[vm->sp - 1] = !vm->data_stack[vm->sp - 1];
  DISPATCH();

// Comparison operations
op_eq:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] == vm->data_stack[vm->sp];
  DISPATCH();
op_neq:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] != vm->data_stack[vm->sp];
  DISPATCH();
op_lt:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] < vm->data_stack[vm->sp];
  DISPATCH();
op_gt:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] > vm->data_stack[vm->sp];
  DISPATCH();
op_lte:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] <= vm->data_stack[vm->sp];
  DISPATCH();
op_gte:
  vm->sp--;
  vm->data_stack[vm->sp - 1] =
      vm->data_stack[vm->sp - 1] >= vm->data_stack[vm->sp];
  DISPATCH();

// Stack operations
op_push_const_u8:
  vm->data_stack[vm->sp++] = (int8_t)vm->code[vm->pc++];
  DISPATCH();
op_push_const_u32:
  memcpy(&tmp, &vm->code[vm->pc], 4);
  vm->pc += 4;
  vm->data_stack[vm->sp++] = (int32_t)tmp;
  DISPATCH();
op_push_const_u64:
  memcpy(&tmp, &vm->code[vm->pc], 8);
  vm->pc += 8;
  vm->data_stack[vm->sp++] = (int64_t)tmp;
  DISPATCH();
op_push_from_local : {
  uint8_t local_id = vm->code[vm->pc++];
  vm->data_stack[vm->sp++] = vm->frame_bp[local_id];
  DISPATCH();
}
op_store_to_local : {
  uint8_t local_id = vm->code[vm->pc++];
  vm->frame_bp[local_id] = vm->data_stack[--vm->sp];
  DISPATCH();
}
op_push_from_addr_b:
  vm->data_stack[vm->sp++] = *(uint8_t*)vm->data_stack[--vm->sp];
  DISPATCH();
op_store_to_addr_b : {
  uint64_t addr = vm->data_stack[--vm->sp];
  uint8_t value = vm->data_stack[--vm->sp];
  *(uint8_t*)addr = value;
  DISPATCH();
}
op_push_from_addr_hw:
  vm->data_stack[vm->sp++] = *(uint16_t*)vm->data_stack[--vm->sp];
  DISPATCH();
op_store_to_addr_hw : {
  uint64_t addr = vm->data_stack[--vm->sp];
  uint16_t value = vm->data_stack[--vm->sp];
  *(uint16_t*)addr = value;
  DISPATCH();
}
op_push_from_addr_w:
  vm->data_stack[vm->sp++] = *(uint32_t*)vm->data_stack[--vm->sp];
  DISPATCH();
op_store_to_addr_w : {
  uint64_t addr = vm->data_stack[--vm->sp];
  uint32_t value = vm->data_stack[--vm->sp];
  *(uint32_t*)addr = value;
  DISPATCH();
}
op_push_from_addr_dw:
  vm->data_stack[vm->sp++] = *(uint64_t*)vm->data_stack[--vm->sp];
  DISPATCH();
op_store_to_addr_dw : {
  uint64_t addr = vm->data_stack[--vm->sp];
  uint64_t value = vm->data_stack[--vm->sp];
  *(uint64_t*)addr = value;
  DISPATCH();
}
op_swap:
  tmp = vm->data_stack[vm->sp - 1];
  vm->data_stack[vm->sp - 1] = vm->data_stack[vm->sp - 2];
  vm->data_stack[vm->sp - 2] = tmp;
  DISPATCH();
op_dup:
  vm->data_stack[vm->sp++] = vm->data_stack[vm->sp - 1];
  DISPATCH();
op_over:
  vm->data_stack[vm->sp++] = vm->data_stack[vm->sp - 2];
  DISPATCH();
op_rot:
  tmp = vm->data_stack[vm->sp - 1];
  vm->data_stack[vm->sp - 1] = vm->data_stack[vm->sp - 2];
  vm->data_stack[vm->sp - 2] = vm->data_stack[vm->sp - 3];
  vm->data_stack[vm->sp - 3] = tmp;
  DISPATCH();
op_drop:
  vm->sp--;
  DISPATCH();
op_jmp:
  memcpy(&jmp_offset, &vm->code[vm->pc], sizeof(int16_t));
  vm->pc += jmp_offset;
  DISPATCH();
op_jmp_if_false:
  memcpy(&jmp_offset, &vm->code[vm->pc], sizeof(int16_t));
  if (vm->data_stack[--vm->sp] == 0)
    vm->pc += jmp_offset;
  else
    vm->pc += sizeof(int16_t);
  DISPATCH();
op_call : {
  int32_t branch_offset;
  const uint8_t args = vm->code[vm->pc++];
  const uint8_t locals = vm->code[vm->pc++];
  memcpy(&branch_offset, &vm->code[vm->pc], sizeof(int32_t));
  vm->pc += sizeof(int32_t);
  const uint64_t* prev_bp = vm->frame_bp;
  // reserve space for return address, previous bp and locals
  vm->frame_bp -= (2 + locals);
  vm->frame_bp[0] = vm->pc;            // return address
  vm->frame_bp[1] = (uint64_t)prev_bp; // previous bp
  // Copy arguments to new frame
  for (uint8_t i = 0; i < args; i++)
    vm->frame_bp[2 + i] = vm->data_stack[vm->sp - args + i];
  vm->sp -= args; // pop arguments from caller frame
  vm->pc += branch_offset;
  DISPATCH();
}
op_ret:
  if (vm->frame_bp == vm->frame_stack + ONDA_VM_FRAME_STACK_SIZE)
    return 0;                                // HALT
  vm->pc = vm->frame_bp[0];                  // return address
  vm->frame_bp = (uint64_t*)vm->frame_bp[1]; // restore previous bp
  DISPATCH();
op_call_native : {
  onda_native_fn_cb_t fn;
  memcpy(&fn, &vm->code[vm->pc], sizeof(uint64_t));
  vm->pc += sizeof(uint64_t);
  fn(&vm->data_stack[vm->sp - 1]); // pass pointer to TOS as argument
  DISPATCH();
}
op_print:
  onda_print_u64((uint64_t)vm->data_stack[--vm->sp]);
  DISPATCH();
op_print_str : {
  onda_print_string((char*)vm->data_stack[--vm->sp]);
  DISPATCH();
}
op_malloc:
  tmp = vm->data_stack[--vm->sp];
  vm->data_stack[vm->sp++] = (uint64_t)onda_malloc(tmp);
  DISPATCH();
op_free:
  tmp = vm->data_stack[--vm->sp];
  onda_free((void*)tmp);
  DISPATCH();
}

void onda_vm_free(onda_vm_t* vm) {
  if (vm->code)
    onda_free(vm->code);
  onda_free(vm);
}
