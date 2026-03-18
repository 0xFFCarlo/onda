#ifndef ONDA_OPTIMIZER_H
#define ONDA_OPTIMIZER_H

#include "onda_compiler.h"

#include <stdbool.h>

// Try to optimize last instructions in the given code object.
// Returns true if any optimization was applied.
bool onda_try_optimize(onda_code_obj_t* cobj);

#endif // ONDA_OPTIMIZER_H
