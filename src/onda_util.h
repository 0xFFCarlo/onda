#ifndef ONDA_UTIL_H
#define ONDA_UTIL_H

#include <stddef.h>
#include <stdint.h>

void* onda_malloc(size_t size);
void* onda_calloc(size_t count, size_t size);
void onda_free(void* ptr);
void* onda_realloc(void* ptr, size_t size);

#endif // ONDA_UTIL_H
