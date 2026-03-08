#include "onda_std.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t* onda_print_u64(int64_t* sp) {
  uint64_t x = (uint64_t)(*sp);
  printf("%llu\n", x);
  return sp + 1;
}

static int64_t* onda_print_string(int64_t* sp) {
  char* str = (char*)(uintptr_t)(*sp);
  printf("%s", str);
  return sp + 1;
}

static int64_t* onda_malloc(int64_t* sp) {
  const size_t size = (size_t)(*sp);
  *sp = (int64_t)(uintptr_t)malloc(size);
  return sp;
}

static int64_t* onda_calloc(int64_t* sp) {
  const size_t num = (size_t)(*sp);
  const size_t size = (size_t)(*(sp + 1));
  *(sp + 1) = (int64_t)(uintptr_t)calloc(num, size);
  return sp + 1;
}

static int64_t* onda_free(int64_t* sp) {
  void* ptr = (void*)(uintptr_t)(*sp);
  free(ptr);
  return sp + 1;
}

static int64_t* onda_realloc(int64_t* sp) {
  void* ptr = (void*)(uintptr_t)(*sp);
  size_t new_size = (size_t)(*(sp + 1));
  *sp = (int64_t)(uintptr_t)realloc(ptr, new_size);
  return sp + 1;
}

static const onda_native_fn_t std_fns[] = {
    {"print", 5, onda_print_u64, 1, 0},
    {"print_str", 9, onda_print_string, 1, 0},
    {"malloc", 6, onda_malloc, 1, 1},
    {"calloc", 6, onda_calloc, 2, 1},
    {"free", 4, onda_free, 1, 0},
    {"realloc", 7, onda_realloc, 2, 1},
};

int onda_env_register_std(onda_env_t* env) {
  for (size_t i = 0; i < sizeof(std_fns) / sizeof(onda_native_fn_t); i++) {
    const onda_native_fn_t* fn = &std_fns[i];
    if (onda_env_register_native_fn(env,
                                    fn->name,
                                    fn->fn,
                                    fn->args_count,
                                    fn->returns_count) != 0) {
      fprintf(stderr,
              "Error: Failed to register standard function '%s'\n",
              fn->name);
      return -1;
    }
  }
  return 0;
}
