#ifndef ONDA_JIT_H
#define ONDA_JIT_H

#include "onda_env.h"

#include <stddef.h>
#include <stdint.h>

uint64_t onda_jit_run(const uint8_t* machine_code, size_t machine_code_size);
int onda_jit_compile(const uint8_t* bytecode,
                     const size_t bytecode_entry_pc,
                     size_t bytecode_size,
                     int64_t* data_sp,
                     int64_t* frame_bp,
                     uint8_t** out_machine_code,
                     size_t* out_machine_code_size,
                     const onda_native_registry_t* reg);

#endif // ONDA_JIT_H
