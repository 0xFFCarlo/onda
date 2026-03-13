#include "onda_vm.h"

#include "onda_env.h"
#include "onda_std.h"
#include "onda_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

onda_vm_t* onda_vm_new(void) {
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
  return 0;
}

static const char* opcode_to_str[ONDA_OP_COUNT] = {
    [ONDA_OP_RET] = "RET",
    [ONDA_OP_ADD] = "ADD",
    [ONDA_OP_SUB] = "SUB",
    [ONDA_OP_MUL] = "MUL",
    [ONDA_OP_DIV] = "DIV",
    [ONDA_OP_MOD] = "MOD",
    [ONDA_OP_ADD_CONST_I8] = "ADD_CONST_I8",
    [ONDA_OP_MUL_CONST_I8] = "MUL_CONST_I8",
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
    [ONDA_OP_INC_LOCAL] = "INC_LOCAL",
    [ONDA_OP_DEC_LOCAL] = "DEC_LOCAL",
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
};

static const uint8_t opcode_args_byte[ONDA_OP_COUNT] = {
    [ONDA_OP_PUSH_CONST_U8] = sizeof(uint8_t),
    [ONDA_OP_PUSH_CONST_U32] = sizeof(uint32_t),
    [ONDA_OP_PUSH_CONST_U64] = sizeof(uint64_t),
    [ONDA_OP_PUSH_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_STORE_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_ADD_CONST_I8] = sizeof(int8_t),
    [ONDA_OP_MUL_CONST_I8] = sizeof(int8_t),
    [ONDA_OP_INC_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_DEC_LOCAL] = sizeof(uint8_t),
    [ONDA_OP_JUMP] = sizeof(int16_t),
    [ONDA_OP_JUMP_IF_FALSE] = sizeof(int16_t),
    [ONDA_OP_CALL] = sizeof(int32_t) + 2,
    [ONDA_OP_CALL_NATIVE] = sizeof(uint32_t),
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

#ifdef ONDA_VM_DEBUG_MODE
static void debug_step(onda_vm_t* vm,
                       size_t pc,
                       int64_t* sp,
                       int64_t* frame_bp) {
  uint8_t opcode = vm->code[pc];
  printf("PC: %04zu, DS_SIZE: %04zu, FBP: %04zu, Opcode: %s\n",
         pc,
         vm->data_stack + ONDA_DATA_STACK_SIZE - sp,
         (vm->frame_stack + ONDA_FRAME_STACK_SIZE) - frame_bp,
         opcode_to_str[opcode]);
  printf("DS: {");
  for (int64_t* data = sp; data < vm->data_stack + ONDA_DATA_STACK_SIZE;
       data++) {
    printf("%lld", *data);
    if (data < vm->data_stack + ONDA_DATA_STACK_SIZE - 1)
      printf(", ");
  }
  printf("}\n");
  printf("FS: {");
  size_t frame_size;
  if (frame_bp == vm->frame_stack + ONDA_FRAME_STACK_SIZE) {
    frame_size = 2;
  } else {
    frame_size = (int64_t*)frame_bp[1] - frame_bp;
  }
  for (size_t i = 0; i < frame_size; i++) {
    printf("%lld", frame_bp[i]);
    if (i < frame_size - 1)
      printf(", ");
  }
  printf("}\n\n");
}
#endif

int onda_vm_run(onda_vm_t* vm) {
  size_t pc = vm->entry_pc;
  int64_t* frame_bp = vm->frame_stack + ONDA_FRAME_STACK_SIZE;
  int64_t* sp = vm->data_stack + ONDA_DATA_STACK_SIZE;

  static const void* dispatch_table[] = {
      [ONDA_OP_RET] = &&op_ret,
      [ONDA_OP_ADD] = &&op_add,
      [ONDA_OP_SUB] = &&op_sub,
      [ONDA_OP_MUL] = &&op_mul,
      [ONDA_OP_DIV] = &&op_div,
      [ONDA_OP_MOD] = &&op_mod,
      [ONDA_OP_ADD_CONST_I8] = &&op_add_const_i8,
      [ONDA_OP_MUL_CONST_I8] = &&op_mul_const_i8,
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
      [ONDA_OP_INC_LOCAL] = &&op_inc_local,
      [ONDA_OP_DEC_LOCAL] = &&op_dec_local,
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
  };

#ifdef ONDA_VM_DEBUG_MODE
#define DISPATCH()                                                             \
  if (vm->debug_mode)                                                          \
    debug_step(vm, pc, sp, frame_bp);                                          \
  goto* dispatch_table[vm->code[pc++]];
#else
#define DISPATCH() goto* dispatch_table[vm->code[pc++]];
#endif

  DISPATCH();

  uint64_t tmp;
  int16_t jmp_offset;

// Arithmetic operations
op_add:
  sp++;
  *sp += *(sp - 1);
  DISPATCH();
op_sub:
  sp++;
  *sp -= *(sp - 1);
  DISPATCH();
op_mul:
  sp++;
  *sp *= *(sp - 1);
  DISPATCH();
op_div:
  sp++;
  *sp /= *(sp - 1);
  DISPATCH();
op_mod:
  sp++;
  *sp %= *(sp - 1);
  DISPATCH();
op_add_const_i8:
  *sp += (int8_t)vm->code[pc++];
  DISPATCH();
op_mul_const_i8:
  *sp *= (int8_t)vm->code[pc++];
  DISPATCH();
op_inc:
  (*sp)++;
  DISPATCH();
op_dec:
  (*sp)--;
  DISPATCH();
op_and:
  sp++;
  *sp = *sp && *(sp - 1);
  DISPATCH();
op_or:
  sp++;
  *sp = *sp || *(sp - 1);
  DISPATCH();
op_not:
  *sp = !*sp;
  DISPATCH();
op_eq:
  sp++;
  *sp = *sp == *(sp - 1);
  DISPATCH();
op_neq:
  sp++;
  *sp = *sp != *(sp - 1);
  DISPATCH();
op_lt:
  sp++;
  *sp = *sp < *(sp - 1);
  DISPATCH();
op_gt:
  sp++;
  *sp = *sp > *(sp - 1);
  DISPATCH();
op_lte:
  sp++;
  *sp = *sp <= *(sp - 1);
  DISPATCH();
op_gte:
  sp++;
  *sp = *sp >= *(sp - 1);
  DISPATCH();
op_push_const_u8:
  sp--;
  *sp = (int8_t)vm->code[pc++];
  DISPATCH();
op_push_const_u32:
  memcpy(&tmp, &vm->code[pc], 4);
  pc += 4;
  sp--;
  *sp = (int32_t)tmp;
  DISPATCH();
op_push_const_u64:
  memcpy(&tmp, &vm->code[pc], 8);
  pc += 8;
  sp--;
  *sp = (int64_t)tmp;
  DISPATCH();
op_push_from_local : {
  uint8_t local_id = vm->code[pc++];
  sp--;
  *sp = frame_bp[local_id];
  DISPATCH();
}
op_store_to_local : {
  frame_bp[vm->code[pc++]] = *sp;
  sp++;
  DISPATCH();
}
op_inc_local : {
  frame_bp[vm->code[pc++]]++;
  DISPATCH();
}
op_dec_local : {
  frame_bp[vm->code[pc++]]--;
  DISPATCH();
}
op_push_from_addr_b:
  *sp = *(uint8_t*)*sp;
  DISPATCH();
op_store_to_addr_b:
  *(uint8_t*)*sp = *(sp + 1);
  sp += 2;
  DISPATCH();
op_push_from_addr_hw:
  *sp = *(uint16_t*)*sp;
  DISPATCH();
op_store_to_addr_hw:
  *(uint16_t*)*sp = *(sp + 1);
  sp += 2;
  DISPATCH();
op_push_from_addr_w:
  *sp = *(uint32_t*)*sp;
  DISPATCH();
op_store_to_addr_w:
  *(uint32_t*)*sp = *(sp + 1);
  sp += 2;
  DISPATCH();
op_push_from_addr_dw:
  *sp = *(uint64_t*)*sp;
  DISPATCH();
op_store_to_addr_dw:
  *(uint64_t*)*sp = *(sp + 1);
  sp += 2;
  DISPATCH();
op_swap:
  tmp = *sp;
  *sp = *(sp + 1);
  *(sp + 1) = tmp;
  DISPATCH();
op_dup:
  sp--;
  *sp = *(sp + 1);
  DISPATCH();
op_over:
  sp--;
  *sp = *(sp + 2);
  DISPATCH();
op_rot:
  tmp = *sp;
  *sp = *(sp + 1);
  *(sp + 1) = *(sp + 2);
  *(sp + 2) = tmp;
  DISPATCH();
op_drop:
  sp++;
  DISPATCH();
op_jmp:
  memcpy(&jmp_offset, &vm->code[pc], sizeof(int16_t));
  pc += jmp_offset;
  DISPATCH();
op_jmp_if_false:
  memcpy(&jmp_offset, &vm->code[pc], sizeof(int16_t));
  if (*sp == 0)
    pc += jmp_offset;
  else
    pc += sizeof(int16_t);
  sp++;
  DISPATCH();
op_call : {
  int32_t branch_offset;
  const uint8_t args = vm->code[pc++];
  const uint8_t locals = vm->code[pc++];
  memcpy(&branch_offset, &vm->code[pc], sizeof(int32_t));
  pc += sizeof(int32_t);
  const int64_t* prev_bp = frame_bp;
  // reserve space for return address, previous bp and locals
  frame_bp -= (2 + locals);
  frame_bp[0] = pc;                // return address
  frame_bp[1] = (uint64_t)prev_bp; // previous bp
  // Copy arguments to new frame
  for (uint8_t i = 0; i < args; i++)
    frame_bp[2 + i] = *(sp + args - i - 1);
  sp += args; // pop arguments from caller frame
  pc += branch_offset;
  DISPATCH();
}
op_call_native : {
  uint32_t idx;
  memcpy(&idx, &vm->code[pc], sizeof(uint32_t));
  pc += sizeof(uint32_t);
  int64_t* new_ds = vm->env->native_registry.items[idx].fn(sp);
  if (new_ds == NULL) { // Check for errors
    fprintf(stderr, "Error: native function returned NULL\n");
    exit(1);
  }
  sp = new_ds;
  DISPATCH();
}
op_ret:
  if (frame_bp == vm->frame_stack + ONDA_FRAME_STACK_SIZE) {
    vm->sp = sp;
    return 0; // HALT
  }
  pc = frame_bp[0];                 // return address
  frame_bp = (int64_t*)frame_bp[1]; // restore previous bp
  DISPATCH();
}

void onda_vm_free(onda_vm_t* vm) {
  if (vm->code)
    onda_free(vm->code);
  onda_free(vm);
}
