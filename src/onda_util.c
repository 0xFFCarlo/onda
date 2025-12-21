#include "onda_util.h"

#include <stdlib.h>

void* onda_malloc(size_t size) {
  return malloc(size);
}

void* onda_calloc(size_t count, size_t size) {
  return calloc(count, size);
}

void onda_free(void* ptr) {
  free(ptr);
}

void* onda_realloc(void* ptr, size_t size) {
  return realloc(ptr, size);
}
