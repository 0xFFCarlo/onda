#ifndef ONDA_RUNTIME_H
#define ONDA_RUNTIME_H

#include "onda_config.h"
#include "onda_env.h"

#include <stddef.h>
#include <stdint.h>

typedef struct onda_runtime {
  const uint8_t* code;
  size_t code_size;
  size_t entry_pc;

  int64_t data_stack[ONDA_DATA_STACK_SIZE];
  int64_t frame_stack[ONDA_FRAME_STACK_SIZE];
  int64_t* data_sp;
  int64_t* frame_bp;

  const onda_native_registry_t* native_registry;
} onda_runtime_t;

static inline void onda_runtime_reset(onda_runtime_t* rt) {
  rt->data_sp = rt->data_stack + ONDA_DATA_STACK_SIZE;
  rt->frame_bp = rt->frame_stack + ONDA_FRAME_STACK_SIZE;
}

#endif // ONDA_RUNTIME_H
