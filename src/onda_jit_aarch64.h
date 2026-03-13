#ifndef ONDA_JIT_AARCH64_H
#define ONDA_JIT_AARCH64_H

#include "onda_runtime.h"

#include <stddef.h>
#include <stdint.h>

size_t onda_jit_aarch64(const onda_runtime_t* rt,
                        uint8_t** out_machine_code,
                        size_t* out_machine_code_size);

#endif // ONDA_JIT_AARCH64_H
