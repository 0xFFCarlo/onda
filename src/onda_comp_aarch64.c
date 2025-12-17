#include "onda_comp_aarch64.h"
#include "onda_util.h"
#include "onda_vm.h"

size_t onda_comp_aarch64(const uint8_t *bytecode, size_t bytecode_size,
                         uint8_t **out_machine_code)
{
  int pos = 0;
  uint8_t *mcode = onda_malloc(1024);

  while (pos < bytecode_size) {
    uint8_t opcode = bytecode[pos];
    // For now, just skip all opcodes
    pos += 1;
    switch (opcode) {
      case ONDA_OP_PUSH_CONST_U8:
        pos += 1; // skip operand
        break;
      case ONDA_OP_PUSH_CONST_U32:
        pos += 4; // skip operand
        break;
      case ONDA_OP_PUSH_CONST_U64:
        pos += 8; // skip operand
        break;
      default:
        // No operands
        break;
    }
  }

  return 0;
}
