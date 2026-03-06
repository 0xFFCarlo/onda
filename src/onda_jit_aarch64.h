#ifndef ONDA_JIT_AARCH64_H
#define ONDA_JIT_AARCH64_H

#include <stddef.h>
#include <stdint.h>

size_t onda_jit_aarch64(const uint8_t* bytecode,
                        const size_t bytecode_entry_pc,
                        size_t bytecode_size,
                        int64_t* frame_bp,
                        uint8_t** out_machine_code,
                        size_t* out_machine_code_size);

#endif // ONDA_JIT_AARCH64_H
