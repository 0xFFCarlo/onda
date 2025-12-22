#ifndef ONDA_JIT_H
#define ONDA_JIT_H

#include <stddef.h>
#include <stdint.h>

uint64_t onda_jit_run(const uint8_t* machine_code, size_t machine_code_size);

#endif // ONDA_JIT_H
