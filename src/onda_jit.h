#ifndef ONDA_JIT_H
#define ONDA_JIT_H

#include "onda_runtime.h"

#include <stddef.h>
#include <stdint.h>

uint64_t onda_jit_run(const uint8_t* machine_code, size_t machine_code_size);
int onda_jit_compile(const onda_runtime_t* rt,
                     uint8_t** out_machine_code,
                     size_t* out_machine_code_size);

#endif // ONDA_JIT_H
