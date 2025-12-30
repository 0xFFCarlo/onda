#ifndef ONDA_VM_H
#define ONDA_VM_H

#include <stddef.h>
#include <stdint.h>

#define ONDA_VM_STACK_SIZE 1024

typedef enum onda_op_type {
  // Control Flow
  ONDA_OP_RET = 0,
  ONDA_OP_JUMP,
  ONDA_OP_JUMP_IF,
  ONDA_OP_DEC_JUMP_IF_NZ,
  // Arithmetic Operations
  ONDA_OP_ADD,
  ONDA_OP_SUB,
  ONDA_OP_MUL,
  ONDA_OP_DIV,
  ONDA_OP_MOD,
  ONDA_OP_INC,
  ONDA_OP_DEC,
  // Logical Operations
  ONDA_OP_AND,
  ONDA_OP_OR,
  ONDA_OP_NOT,
  // Comparison Operations
  ONDA_OP_EQ,
  ONDA_OP_NEQ,
  ONDA_OP_LT,
  ONDA_OP_GT,
  ONDA_OP_LTE,
  ONDA_OP_GTE,
  // Stack Operations
  ONDA_OP_PUSH_CONST_U8,
  ONDA_OP_PUSH_CONST_U32,
  ONDA_OP_PUSH_CONST_U64,
  ONDA_OP_SWAP,
  ONDA_OP_DUP,
  ONDA_OP_OVER,
  ONDA_OP_ROT,
  ONDA_OP_DROP,
  // I/O Operations
  ONDA_OP_PRINT,
  ONDA_OP_PRINT_STR,
} onda_op_type_t;

typedef struct onda_vm {
  uint8_t* code;
  size_t code_size;
  size_t entry_pc;
  size_t pc;

  uint64_t stack[ONDA_VM_STACK_SIZE];
  size_t sp;
} onda_vm_t;

onda_vm_t* onda_vm_new();
int onda_vm_load_code(onda_vm_t* vm,
                      const uint8_t* code,
                      const size_t entry_pc,
                      const size_t code_size);
void onda_vm_print_bytecode(const uint8_t* code, size_t code_size);
int onda_vm_run(onda_vm_t* vm);
void onda_vm_free(onda_vm_t* vm);

#endif // ONDA_VM_H
