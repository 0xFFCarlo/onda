#ifndef ONDA_DICT_H
#define ONDA_DICT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char* key;
  size_t key_len;
  uint64_t value;
} onda_dict_slot_t;

typedef struct {
  onda_dict_slot_t* slots;
  size_t capacity;
  size_t count;
} onda_dict_t;

void onda_dict_init(onda_dict_t* d);
void onda_dict_free(onda_dict_t* d);
int onda_dict_get(onda_dict_t* d,
                  const char* key,
                  size_t key_len,
                  uint64_t* out);
void onda_dict_put(onda_dict_t* d,
                   const char* key,
                   size_t key_len,
                   uint64_t value);

#endif // ONDA_DICT_H
