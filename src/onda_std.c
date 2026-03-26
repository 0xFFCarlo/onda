#include "onda_std.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t* onda_print_u64(int64_t* sp, size_t depth) {
  (void)depth;
  uint64_t x = (uint64_t)(*sp);
  printf("%" PRIu64, x);
  return sp + 1;
}

static int64_t* onda_print_i64(int64_t* sp, size_t depth) {
  (void)depth;
  int64_t x = *sp;
  printf("%" PRId64, x);
  return sp + 1;
}

static int64_t* onda_print_hex(int64_t* sp, size_t depth) {
  (void)depth;
  uint64_t x = (uint64_t)(*sp);
  printf("0x%" PRIx64, x);
  return sp + 1;
}

static int64_t* onda_print_ptr(int64_t* sp, size_t depth) {
  (void)depth;
  void* ptr = (void*)(uintptr_t)(*sp);
  printf("%p\n", ptr);
  return sp + 1;
}

static int64_t* onda_print_char(int64_t* sp, size_t depth) {
  (void)depth;
  char c = (char)(*sp);
  printf("%c\n", c);
  return sp + 1;
}

static int64_t* onda_print_string(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*sp);
  printf("%s", str);
  return sp + 1;
}

static int64_t* onda_stack_depth(int64_t* sp,
                                 size_t depth) {
  sp--;
  *sp = (int64_t)depth;
  return sp;
}

static int64_t* onda_print_stack(int64_t* sp, size_t depth) {
  printf("DS: {");
  for (size_t i = 0; i < depth; i++) {
    printf("%" PRId64, sp[i]);
    if (i + 1 < depth)
      printf(", ");
  }
  printf("}\n");
  return sp;
}

static int64_t* onda_malloc(int64_t* sp, size_t depth) {
  (void)depth;
  const size_t size = (size_t)(*sp);
  *sp = (int64_t)(uintptr_t)malloc(size);
  return sp;
}

static int64_t* onda_calloc(int64_t* sp, size_t depth) {
  (void)depth;
  const size_t num = (size_t)(*(sp + 1));
  const size_t size = (size_t)(*sp);
  *(sp + 1) = (int64_t)(uintptr_t)calloc(num, size);
  return sp + 1;
}

static int64_t* onda_free(int64_t* sp, size_t depth) {
  (void)depth;
  void* ptr = (void*)(uintptr_t)(*sp);
  free(ptr);
  return sp + 1;
}

static int64_t* onda_realloc(int64_t* sp, size_t depth) {
  (void)depth;
  void* ptr = (void*)(uintptr_t)(*(sp + 1));
  size_t new_size = (size_t)(*sp);
  *(sp + 1) = (int64_t)(uintptr_t)realloc(ptr, new_size);
  return sp + 1;
}

static int64_t* onda_memcpy(int64_t* sp, size_t depth) {
  (void)depth;
  void* dest = (void*)(uintptr_t)(*(sp + 2));
  void* src = (void*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*sp);
  memcpy(dest, src, n);
  return sp + 3;
}

static int64_t* onda_memset(int64_t* sp, size_t depth) {
  (void)depth;
  void* dest = (void*)(uintptr_t)(*(sp + 2));
  int value = (int)(*(sp + 1));
  size_t n = (size_t)(*sp);
  memset(dest, value, n);
  return sp + 3;
}

static int64_t* onda_memcmp(int64_t* sp, size_t depth) {
  (void)depth;
  void* ptr1 = (void*)(uintptr_t)(*(sp + 2));
  void* ptr2 = (void*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*sp);
  int result = memcmp(ptr1, ptr2, n);
  *(sp + 2) = (int64_t)result;
  return sp + 2;
}

static int64_t* onda_strlen(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*sp);
  *sp = (int64_t)strlen(str);
  return sp;
}

static int64_t* onda_strcmp(int64_t* sp, size_t depth) {
  (void)depth;
  char* str1 = (char*)(uintptr_t)(*(sp + 1));
  char* str2 = (char*)(uintptr_t)(*sp);
  int result = strcmp(str1, str2);
  *(sp + 1) = (int64_t)result;
  return sp + 1;
}

static int64_t* onda_strncmp(int64_t* sp, size_t depth) {
  (void)depth;
  char* str1 = (char*)(uintptr_t)(*(sp + 2));
  char* str2 = (char*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*sp);
  int result = strncmp(str1, str2, n);
  *(sp + 2) = (int64_t)result;
  return sp + 2;
}

static int64_t* onda_strcpy(int64_t* sp, size_t depth) {
  (void)depth;
  char* dest = (char*)(uintptr_t)(*(sp + 1));
  char* src = (char*)(uintptr_t)(*sp);
  strcpy(dest, src);
  return sp + 2;
}

static int64_t* onda_strncpy(int64_t* sp, size_t depth) {
  (void)depth;
  char* dest = (char*)(uintptr_t)(*(sp + 2));
  char* src = (char*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*sp);
  strncpy(dest, src, n);
  return sp + 3;
}

static int64_t* onda_strcat(int64_t* sp, size_t depth) {
  (void)depth;
  char* dest = (char*)(uintptr_t)(*(sp + 1));
  char* src = (char*)(uintptr_t)(*sp);
  strcat(dest, src);
  return sp + 2;
}

static int64_t* onda_strncat(int64_t* sp, size_t depth) {
  (void)depth;
  char* dest = (char*)(uintptr_t)(*(sp + 2));
  char* src = (char*)(uintptr_t)(*(sp + 1));
  size_t n = (size_t)(*sp);
  strncat(dest, src, n);
  return sp + 3;
}

static int64_t* onda_strchr(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*(sp + 1));
  int ch = (int)(*sp);
  *(sp + 1) = (int64_t)(uintptr_t)strchr(str, ch);
  return sp + 1;
}

static int64_t* onda_strstr(int64_t* sp, size_t depth) {
  (void)depth;
  char* haystack = (char*)(uintptr_t)(*(sp + 1));
  char* needle = (char*)(uintptr_t)(*sp);
  *(sp + 1) = (int64_t)(uintptr_t)strstr(haystack, needle);
  return sp + 1;
}

static int64_t* onda_fopen(int64_t* sp, size_t depth) {
  (void)depth;
  char* filename = (char*)(uintptr_t) * (sp + 1);
  char* mode = (char*)(uintptr_t)(*(sp));
  FILE* file = fopen(filename, mode);
  *(sp + 1) = (int64_t)(uintptr_t)file;
  return sp + 1;
}

static int64_t* onda_tmpfile(int64_t* sp, size_t depth) {
  (void)depth;
  sp--;
  *sp = (int64_t)(uintptr_t)tmpfile();
  return sp;
}

static int64_t* onda_fclose(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* file = (FILE*)(uintptr_t)(*sp);
  int result = fclose(file);
  *sp = (int64_t)result;
  return sp;
}

static int64_t* onda_fread(int64_t* sp, size_t depth) {
  (void)depth;
  void* ptr = (void*)(uintptr_t) * (sp + 3);
  size_t size = (size_t)(*(sp + 2));
  size_t nmemb = (size_t)(*(sp + 1));
  FILE* stream = (FILE*)(uintptr_t)(*(sp));
  size_t result = fread(ptr, size, nmemb, stream);
  *(sp + 3) = (int64_t)result;
  return sp + 3;
}

static int64_t* onda_fwrite(int64_t* sp, size_t depth) {
  (void)depth;
  const void* ptr = (const void*)(uintptr_t) * (sp + 3);
  size_t size = (size_t)(*(sp + 2));
  size_t nmemb = (size_t)(*(sp + 1));
  FILE* stream = (FILE*)(uintptr_t)(*(sp));
  size_t result = fwrite(ptr, size, nmemb, stream);
  *(sp + 3) = (int64_t)result;
  return sp + 3;
}

static int64_t* onda_fseek(int64_t* sp, size_t depth) {
  (void)depth;
  int whence = (int)(*sp);
  long offset = (long)(*(sp + 1));
  FILE* stream = (FILE*)(uintptr_t)(*(sp + 2));
  *(sp + 2) = (int64_t)fseek(stream, offset, whence);
  return sp + 2;
}

static int64_t* onda_ftell(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  *sp = (int64_t)ftell(stream);
  return sp;
}

static int64_t* onda_fflush(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  *sp = (int64_t)fflush(stream);
  return sp;
}

static int64_t* onda_feof(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  *sp = (int64_t)feof(stream);
  return sp;
}

static int64_t* onda_ferror(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  *sp = (int64_t)ferror(stream);
  return sp;
}

static int64_t* onda_rewind(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  rewind(stream);
  return sp + 1;
}

static int64_t* onda_clearerr(int64_t* sp, size_t depth) {
  (void)depth;
  FILE* stream = (FILE*)(uintptr_t)(*sp);
  clearerr(stream);
  return sp + 1;
}

static int64_t* onda_remove(int64_t* sp, size_t depth) {
  (void)depth;
  char* filename = (char*)(uintptr_t)(*sp);
  *sp = (int64_t)remove(filename);
  return sp;
}

static int64_t* onda_rename(int64_t* sp, size_t depth) {
  (void)depth;
  char* old_path = (char*)(uintptr_t)(*(sp + 1));
  char* new_path = (char*)(uintptr_t)(*sp);
  *(sp + 1) = (int64_t)rename(old_path, new_path);
  return sp + 1;
}

static int64_t* onda_atoi(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*sp);
  *sp = (int64_t)atoi(str);
  return sp;
}

static int64_t* onda_strtol(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*(sp + 1));
  int base = (int)(*sp);
  char* endptr = NULL;
  *(sp + 1) = (int64_t)strtol(str, &endptr, base);
  return sp + 1;
}

static int64_t* onda_strtoul(int64_t* sp, size_t depth) {
  (void)depth;
  char* str = (char*)(uintptr_t)(*(sp + 1));
  int base = (int)(*sp);
  char* endptr = NULL;
  *(sp + 1) = (int64_t)strtoul(str, &endptr, base);
  return sp + 1;
}

static int64_t* onda_exit(int64_t* sp, size_t depth) {
  (void)depth;
  fprintf(stderr, "Error: 'exit' is not supported in this runtime\n");
  (void)sp;
  return NULL;
}

static int64_t* onda_assert(int64_t* sp, size_t depth) {
  (void)depth;
  const char* msg = (const char*)(uintptr_t)(*sp);
  const int64_t cond = *(sp + 1);
  if (!cond) {
    fprintf(stderr, "Assertion failed: %s\n", msg ? msg : "(null)");
    return NULL;
  }
  return sp + 2;
}

static const onda_native_fn_t std_fns[] = {
    {".", 5, onda_print_u64, 1, 0},
    {"depth", 5, onda_stack_depth, 0, 1},
    {".stack", 6, onda_print_stack, 0, 0},
    {".s", 5, onda_print_string, 1, 0},
    {".c", 5, onda_print_char, 1, 0},
    {"print_u64", 5, onda_print_u64, 1, 0},
    {"print_i64", 9, onda_print_i64, 1, 0},
    {"print_hex", 9, onda_print_hex, 1, 0},
    {"print_ptr", 9, onda_print_ptr, 1, 0},
    {"print_char", 10, onda_print_char, 1, 0},
    {"print_str", 9, onda_print_string, 1, 0},
    {"emit", 4, onda_print_char, 1, 0},
    {"malloc", 6, onda_malloc, 1, 1},
    {"calloc", 6, onda_calloc, 2, 1},
    {"free", 4, onda_free, 1, 0},
    {"realloc", 7, onda_realloc, 2, 1},
    {"memcpy", 6, onda_memcpy, 3, 0},
    {"memset", 6, onda_memset, 3, 0},
    {"memcmp", 6, onda_memcmp, 3, 1},
    {"strlen", 6, onda_strlen, 1, 1},
    {"strcmp", 6, onda_strcmp, 2, 1},
    {"strncmp", 7, onda_strncmp, 3, 1},
    {"strcpy", 6, onda_strcpy, 2, 0},
    {"strncpy", 7, onda_strncpy, 3, 0},
    {"strcat", 6, onda_strcat, 2, 0},
    {"strncat", 7, onda_strncat, 3, 0},
    {"strchr", 6, onda_strchr, 2, 1},
    {"strstr", 6, onda_strstr, 2, 1},
    {"atoi", 4, onda_atoi, 1, 1},
    {"strtol", 6, onda_strtol, 2, 1},
    {"strtoul", 7, onda_strtoul, 2, 1},
    {"fopen", 5, onda_fopen, 2, 1},
    {"tmpfile", 7, onda_tmpfile, 0, 1},
    {"fclose", 6, onda_fclose, 1, 1},
    {"fread", 5, onda_fread, 4, 1},
    {"fwrite", 6, onda_fwrite, 4, 1},
    {"fseek", 5, onda_fseek, 3, 1},
    {"ftell", 5, onda_ftell, 1, 1},
    {"fflush", 6, onda_fflush, 1, 1},
    {"feof", 4, onda_feof, 1, 1},
    {"ferror", 6, onda_ferror, 1, 1},
    {"rewind", 6, onda_rewind, 1, 0},
    {"clearerr", 8, onda_clearerr, 1, 0},
    {"remove", 6, onda_remove, 1, 1},
    {"rename", 6, onda_rename, 2, 1},
    {"exit", 4, onda_exit, 1, 0},
    {"assert", 6, onda_assert, 2, 0},
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
