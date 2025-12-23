#include "onda_dict.h"

#include "onda_util.h"

#include <string.h>

static inline uint32_t hash_str(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619;
  }
  return h;
}

static void onda_dict_resize(onda_dict_t* d, size_t new_cap) {
  onda_dict_slot_t* old = d->slots;
  size_t old_cap = d->capacity;

  d->slots = onda_calloc(new_cap, sizeof(onda_dict_slot_t));
  d->capacity = new_cap;
  d->count = 0;

  for (size_t i = 0; i < old_cap; i++) {
    if (old[i].key)
      onda_dict_put(d, old[i].key, old[i].value);
  }
  onda_free(old);
}

void onda_dict_init(onda_dict_t* d) {
  d->slots = NULL;
  d->capacity = 0;
  d->count = 0;
}

void onda_dict_free(onda_dict_t* d) {
  if (!d->slots)
    return;

  for (size_t i = 0; i < d->capacity; i++)
    onda_free(d->slots[i].key);
  onda_free(d->slots);

  d->slots = NULL;
  d->capacity = 0;
  d->count = 0;
}

int onda_dict_get(onda_dict_t* d, const char* key, uint32_t* out) {
  if (!d->slots)
    return 0;

  uint32_t h = hash_str(key);
  size_t mask = d->capacity - 1;

  for (size_t i = 0; i < d->capacity; i++) {
    size_t idx = (h + i) & mask;
    onda_dict_slot_t* s = &d->slots[idx];

    if (!s->key)
      return 0;
    if (strcmp(s->key, key) == 0) {
      *out = s->value;
      return 1;
    }
  }
  return 0;
}

void onda_dict_put(onda_dict_t* d, const char* key, uint32_t value) {
  if (d->count * 2 >= d->capacity)
    onda_dict_resize(d, d->capacity ? d->capacity * 2 : 16);

  uint32_t h = hash_str(key);
  size_t mask = d->capacity - 1;

  for (size_t i = 0;; i++) {
    size_t idx = (h + i) & mask;
    onda_dict_slot_t* s = &d->slots[idx];

    if (!s->key) {
      s->key = strdup(key);
      s->value = value;
      d->count++;
      return;
    }
    if (strcmp(s->key, key) == 0) {
      s->value = value; // optional overwrite
      return;
    }
  }
}
