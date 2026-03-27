#include "onda_env.h"

#include "onda_util.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int onda_env_init(onda_env_t* env) {
  env->native_registry.items = NULL;
  env->native_registry.count = 0;
  onda_dict_init(&env->native_registry.items_map);
  return 0;
}

// Free the environment and its resources
void onda_env_free(onda_env_t* env) {
  if (env->native_registry.items)
    onda_free(env->native_registry.items);
  onda_dict_free(&env->native_registry.items_map);
  memset(env, 0, sizeof(onda_env_t));
}

static inline bool is_power_two(size_t x) {
  return (x & (x - 1)) == 0 && x != 0;
}

// Register a native function in the environment's native function table
int onda_env_register_native_fn(onda_env_t* env,
                                const char* name,
                                onda_native_fn_cb_t fn,
                                uint8_t args_count,
                                uint8_t returns_count) {
  if (onda_native_fn_get(&env->native_registry, name, strlen(name)) != NULL) {
    fprintf(stderr, "Error: Native function '%s' already registered\n", name);
    return -1;
  }
  size_t idx = env->native_registry.count;
  onda_native_fn_t* items = env->native_registry.items;
  if (is_power_two(idx + 1) || idx == 0) {
    size_t new_capacity = (idx + 1) * 2;
    items = onda_realloc(env->native_registry.items,
                         new_capacity * sizeof(onda_native_fn_t));
    if (!items) {
      fprintf(stderr,
              "Error: Failed to allocate memory for native function '%s'\n",
              name);
      return -1;
    }
    env->native_registry.items = items;
  }
  items[idx].name = name;
  items[idx].name_len = (uint8_t)strlen(name);
  items[idx].fn = fn;
  items[idx].args_count = args_count;
  items[idx].returns_count = returns_count;
  env->native_registry.items = items;
  env->native_registry.count++;
  onda_dict_put_borrowed(&env->native_registry.items_map,
                         name,
                         strlen(name),
                         idx);
  return 0;
}
