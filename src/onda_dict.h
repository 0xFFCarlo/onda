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

// Initialize the dictionary
void onda_dict_init(onda_dict_t* d);

// Free the dictionary's resources
void onda_dict_free(onda_dict_t* d);

// Get the value associated with a key. Returns 1 if found, 0 otherwise.
int onda_dict_get(onda_dict_t* d,
                  const char* key,
                  size_t key_len,
                  uint64_t* out);

// Put a key-value pair into the dictionary. If the key already exists, its
// value is updated.
void onda_dict_put(onda_dict_t* d,
                   const char* key,
                   size_t key_len,
                   uint64_t value);

#endif // ONDA_DICT_H
