#ifndef ONDA_COMP_AARCH64_H
#define ONDA_COMP_AARCH64_H

#include <stddef.h>
#include <stdint.h>

size_t onda_comp_aarch64(const uint8_t *bytecode, size_t bytecode_size,
                         uint8_t **out_machine_code);

#endif // ONDA_COMP_AARCH64_H
