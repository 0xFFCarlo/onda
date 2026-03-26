#ifndef ONDA_ENV_H
#define ONDA_ENV_H

#include "onda_dict.h"

#include <stddef.h>
#include <stdint.h>

// Native function pointer to C code
typedef int64_t* (*onda_native_fn_cb_t)(int64_t* data_stack,
                                        size_t stack_depth);

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
} onda_native_registry_t;

typedef struct {
  onda_native_registry_t native_registry;
} onda_env_t;

// Initialize the environment
int onda_env_init(onda_env_t* env);

// Free the environment and its resources
void onda_env_free(onda_env_t* env);

// Register a native function in the environment's native function registry
int onda_env_register_native_fn(onda_env_t* env,
                                const char* name,
                                onda_native_fn_cb_t fn,
                                uint8_t args_count,
                                uint8_t returns_count);

// Get native function by name from the native function registry
static inline onda_native_fn_t* onda_native_fn_get(onda_native_registry_t* reg,
                                                   const char* name,
                                                   size_t name_len) {
  uint64_t idx;
  if (onda_dict_get(&reg->items_map, name, name_len, &idx) == 0)
    return &reg->items[idx];
  return NULL; // not found
}

#endif // ONDA_ENV_H
