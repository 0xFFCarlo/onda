#ifndef ONDA_VM_H
#define ONDA_VM_H

#include "onda_config.h"
#include "onda_dict.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum onda_op_type {
  // Control Flow
  ONDA_OP_RET = 0,
  ONDA_OP_JUMP,
  ONDA_OP_JUMP_IF_FALSE,
  ONDA_OP_CALL,
  ONDA_OP_CALL_NATIVE,
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
  ONDA_OP_PUSH_LOCAL,
  ONDA_OP_STORE_LOCAL,
  ONDA_OP_PUSH_FROM_ADDR_B,
  ONDA_OP_STORE_TO_ADDR_B,
  ONDA_OP_PUSH_FROM_ADDR_HW,
  ONDA_OP_STORE_TO_ADDR_HW,
  ONDA_OP_PUSH_FROM_ADDR_W,
  ONDA_OP_STORE_TO_ADDR_W,
  ONDA_OP_PUSH_FROM_ADDR_DW,
  ONDA_OP_STORE_TO_ADDR_DW,
  ONDA_OP_SWAP,
  ONDA_OP_DUP,
  ONDA_OP_OVER,
  ONDA_OP_ROT,
  ONDA_OP_DROP,
  // I/O Operations
  ONDA_OP_PRINT,
  ONDA_OP_PRINT_STR,
  ONDA_OP_MALLOC,
  ONDA_OP_FREE,
  ONDA_OP_COUNT,
} onda_op_type_t;

typedef struct onda_vm {
  uint8_t* code;
  size_t code_size;
  size_t entry_pc;
  size_t pc;

  int64_t data_stack[ONDA_DATA_STACK_SIZE];
  size_t sp; // top of stack index (points to next free slot)

  int64_t frame_stack[ONDA_FRAME_STACK_SIZE];
  int64_t* frame_bp;
  int64_t* frame_prev_bp;

  bool debug_mode;
} onda_vm_t;

// Native function pointer to C code
typedef int64_t* (*onda_native_fn_cb_t)(int64_t* data_stack);

// Stores native function and its name
typedef struct {
  const char* name;
  uint8_t name_len;
  onda_native_fn_cb_t fn;
  // For documentation
  uint8_t args_count;
  uint8_t returns_count;
} onda_native_fn_t;

typedef struct {
  onda_native_fn_t* items;
  size_t count;
  onda_dict_t items_map;
} onda_native_table_t;

// Allocate a new VM
onda_vm_t* onda_vm_new(void);

// Load code into the VM
int onda_vm_load_code(onda_vm_t* vm,
                      const uint8_t* code,
                      const size_t entry_pc,
                      const size_t code_size);

// Print the VM bytecode in human readable format
void onda_vm_print_bytecode(const uint8_t* code, size_t code_size);

// Execute VM bytecode
int onda_vm_run(onda_vm_t* vm);

// Free VM
void onda_vm_free(onda_vm_t* vm);

// Get native function by name from the native function table
static inline onda_native_fn_t* onda_native_fn_get(onda_native_table_t* table,
                                                   const char* name,
                                                   size_t name_len) {
  uint64_t idx;
  if (onda_dict_get(&table->items_map, name, name_len, &idx) == 0)
    return &table->items[idx];
  return NULL; // not found
}

#endif // ONDA_VM_H
