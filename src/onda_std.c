#include "onda_std.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t* onda_print_u64(int64_t* sp) {
  uint64_t x = (uint64_t)(*sp);
  printf("%" PRIu64 "\n", x);
  return sp + 1;
}

static int64_t* onda_print_i64(int64_t* sp) {
  int64_t x = *sp;
  printf("%" PRId64 "\n", x);
  return sp + 1;
}

static int64_t* onda_print_hex(int64_t* sp) {
  uint64_t x = (uint64_t)(*sp);
  printf("0x%" PRIx64 "\n", x);
  return sp + 1;
}

static int64_t* onda_print_ptr(int64_t* sp) {
  void* ptr = (void*)(uintptr_t)(*sp);
  printf("%p\n", ptr);
  return sp + 1;
}

static int64_t* onda_print_char(int64_t* sp) {
  char c = (char)(*sp);
  printf("%c\n", c);
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
  *(sp + 1) = (int64_t)(uintptr_t)realloc(ptr, new_size);
  return sp + 1;
}

static int64_t* onda_memcpy(int64_t* sp) {
  void* dest = (void*)(uintptr_t)(*sp);
  void* src = (void*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*(sp + 2));
  memcpy(dest, src, n);
  return sp + 3;
}

static int64_t* onda_memset(int64_t* sp) {
  void* dest = (void*)(uintptr_t)(*sp);
  int value = (int)(*(sp + 1));
  size_t n = (size_t)(*(sp + 2));
  memset(dest, value, n);
  return sp + 3;
}

static int64_t* onda_memcmp(int64_t* sp) {
  void* ptr1 = (void*)(uintptr_t)(*sp);
  void* ptr2 = (void*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*(sp + 2));
  int result = memcmp(ptr1, ptr2, n);
  *sp = (int64_t)result;
  return sp + 3;
}

static int64_t* onda_strlen(int64_t* sp) {
  char* str = (char*)(uintptr_t)(*sp);
  *sp = (int64_t)strlen(str);
  return sp;
}

static int64_t* onda_strcmp(int64_t* sp) {
  char* str1 = (char*)(uintptr_t)(*sp);
  char* str2 = (char*)(uintptr_t)(*(sp + 1));
  int result = strcmp(str1, str2);
  *sp = (int64_t)result;
  return sp + 1;
}

static int64_t* onda_strcpy(int64_t* sp) {
  char* dest = (char*)(uintptr_t)(*sp);
  char* src = (char*)(uintptr_t)(*(sp + 1));
  strcpy(dest, src);
  return sp + 2;
}

static int64_t* onda_strcat(int64_t* sp) {
  char* dest = (char*)(uintptr_t)(*sp);
  char* src = (char*)(uintptr_t)(*(sp + 1));
  strcat(dest, src);
  return sp + 2;
}

static int64_t* onda_fopen(int64_t* sp) {
  char* filename = (char*)(uintptr_t) * (sp + 1);
  char* mode = (char*)(uintptr_t)(*(sp));
  FILE* file = fopen(filename, mode);
  *(sp + 1) = (int64_t)(uintptr_t)file;
  return sp + 1;
}

static int64_t* onda_fclose(int64_t* sp) {
  FILE* file = (FILE*)(uintptr_t)(*sp);
  int result = fclose(file);
  *sp = (int64_t)result;
  return sp + 1;
}

static int64_t* onda_fread(int64_t* sp) {
  void* ptr = (void*)(uintptr_t) * (sp + 3);
  size_t size = (size_t)(*(sp + 2));
  size_t nmemb = (size_t)(*(sp + 1));
  FILE* stream = (FILE*)(uintptr_t)(*(sp));
  size_t result = fread(ptr, size, nmemb, stream);
  *(sp + 3) = (int64_t)result;
  return sp + 3;
}

static int64_t* onda_fwrite(int64_t* sp) {
  const void* ptr = (const void*)(uintptr_t) * (sp + 3);
  size_t size = (size_t)(*(sp + 2));
  size_t nmemb = (size_t)(*(sp + 1));
  FILE* stream = (FILE*)(uintptr_t)(*(sp));
  size_t result = fwrite(ptr, size, nmemb, stream);
  *(sp + 3) = (int64_t)result;
  return sp + 3;
}

static int64_t* onda_exit(int64_t* sp) {
  int64_t code = *sp;
  exit((int)code);
  return sp + 1; // never reached
}

static const onda_native_fn_t std_fns[] = {
    {".", 5, onda_print_u64, 1, 0},
    {".s", 5, onda_print_string, 1, 0},
    {".c", 5, onda_print_char, 1, 0},
    {"print_u64", 5, onda_print_u64, 1, 0},
    {"print_i64", 9, onda_print_i64, 1, 0},
    {"print_hex", 9, onda_print_hex, 1, 0},
    {"print_ptr", 9, onda_print_ptr, 1, 0},
    {"print_char", 10, onda_print_char, 1, 0},
    {"print_str", 9, onda_print_string, 1, 0},
    {"malloc", 6, onda_malloc, 1, 1},
    {"calloc", 6, onda_calloc, 2, 1},
    {"free", 4, onda_free, 1, 0},
    {"realloc", 7, onda_realloc, 2, 1},
    {"memcpy", 6, onda_memcpy, 3, 0},
    {"memset", 6, onda_memset, 3, 0},
    {"memcmp", 6, onda_memcmp, 3, 1},
    {"strlen", 6, onda_strlen, 1, 1},
    {"strcmp", 6, onda_strcmp, 2, 1},
    {"strcpy", 6, onda_strcpy, 2, 0},
    {"strcat", 6, onda_strcat, 2, 0},
    {"fopen", 5, onda_fopen, 2, 1},
    {"fclose", 6, onda_fclose, 1, 1},
    {"fread", 5, onda_fread, 4, 1},
    {"fwrite", 6, onda_fwrite, 4, 1},
    {"exit", 4, onda_exit, 1, 0},
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
