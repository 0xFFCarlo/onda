#include "onda_comp_aarch64.h"

#include "onda_std.h"
#include "onda_util.h"
#include "onda_vm.h"
#include <stdio.h>

#define ONDA_MCODE_CAPACITY_INIT 512
#define ONDA_MAX_OP_INSTR_COUNT  5 // max instructions per opcode

#define AARCH64_PUSH_X0_STACK    0xF81F8FE0 // str x0, [sp, #-8]!
#define AARCH64_STORE_X0_STACK   0xF90003E0 // str x0, [sp]
#define AARCH64_POP_X0_STACK     0xF84087E0 // ldr x0, [sp], #8
#define AARCH64_POP_X1_STACK     0xF84087E1 // ldr x1, [sp], #8
#define AARCH64_LOAD_X1_STACK    0xF94003E1 // ldr x1, [sp]
#define AARCH64_RET              0xD65F03C0 // ret
#define AARCH64_MOV_X0_X1        0xAA0103E0 // mov x0, x1
#define AARCH64_MOV_X0_X2        0xAA0203E0 // mov x0, x2
#define AARCH64_ADD_X0_X1_X0     0x8B000020 // add x0, x1, x0
#define AARCH64_SUB_X0_X1_X0     0xCB000020 // sub x0, x1, x0
#define AARCH64_MUL_X0_X1_X0     0x9B007C20 // mul x0, x1, x0
#define AARCH64_UDIV_X0_X1_X0    0x9AC00820 // udiv x0, x1, x0
#define AARCH64_UDIV_X2_X1_X0    0x9AC00822 // udiv x2, x1, x0
#define AARCH64_MSUB_X0_X2_X0_X1 0x9B008440 // msub x0, x2, x0, x1
#define AARCH64_INC_X0           0x91000400 // add x0, x0, #1
#define AARCH64_DEC_X0           0xD1000400 // sub x0, x0, #1
#define AARCH64_AND_X0_X1_X0     0x8A000020 // and x0, x1, x0
#define AARCH64_ORR_X0_X1_X0     0xAA000020 // orr x0, x1, x0
#define AARCH64_NOT_X0_X0        0xAA2003E0 // orn x0, x0, xzr
#define AARCH64_CMP_X0_X1        0xEB00003F // cmp x0, x1
#define AARCH64_CSET_X0_EQ       0xE000003F // cset x0, eq
#define AARCH64_CSET_X0_NE       0x9A9F17E0 // cset x0, ne
#define AARCH64_CSET_X0_LT       0x9A9FA7E0 // cset x0, lt
#define AARCH64_CSET_X0_GT       0x9A9F97E0 // cset x0, gt
#define AARCH64_CSET_X0_LTE      0x9A9FB7E0 // cset x0, le
#define AARCH64_CSET_X0_GTE      0x9A9F87E0 // cset x0, ge

// move 16-bit immediate into 64-bit register and zero other bits
// movz x0, imm16, lsl #shift
static inline uint32_t a64_movz_x(int rd, uint16_t imm16, int shift) {
  const int hw = shift / 16; // 0..3
  return 0xD2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (rd & 31);
}

// move 16-bit immediate into 64-bit register, keeping other bits
// movk x0, imm16, lsl #shift
static inline uint32_t a64_movk_x(int rd, uint16_t imm16, int shift) {
  const int hw = shift / 16;
  return 0xF2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (rd & 31);
}

size_t onda_comp_aarch64(const uint8_t* bytecode,
                         size_t bytecode_size,
                         uint8_t** out_machine_code,
                         size_t* out_machine_code_size) {
  int pos = 0;
  uint32_t* mcode = onda_malloc(ONDA_MCODE_CAPACITY_INIT);
  size_t mcode_capacity = ONDA_MCODE_CAPACITY_INIT;
  size_t mcode_size = 0;

  while (pos < bytecode_size) {

    // Ensure capacity
    if (mcode_size + ONDA_MAX_OP_INSTR_COUNT >= mcode_capacity) {
      mcode_capacity *= 2;
      mcode = onda_realloc(mcode, mcode_capacity);
    }

    const uint8_t opcode = bytecode[pos++];
    switch (opcode) {
    case ONDA_OP_PUSH_CONST_U8: {
      const uint8_t imm = bytecode[pos++];
      mcode[mcode_size++] = AARCH64_PUSH_X0_STACK;
      mcode[mcode_size++] = a64_movz_x(0, imm, 0);
    } break;
    case ONDA_OP_PUSH_CONST_U32: {
      uint16_t lo = bytecode[pos++];
      lo |= bytecode[pos++] << 8;
      uint16_t hi = bytecode[pos++];
      hi |= bytecode[pos++] << 8;
      mcode[mcode_size++] = AARCH64_PUSH_X0_STACK;
      mcode[mcode_size++] = a64_movz_x(0, lo, 0);
      if (hi)
        mcode[mcode_size++] = a64_movk_x(0, hi, 16);
    } break;
    case ONDA_OP_PUSH_CONST_U64: {
      uint16_t hw0 = bytecode[pos++];
      hw0 |= bytecode[pos++] << 8;
      uint16_t hw1 = bytecode[pos++];
      hw1 |= bytecode[pos++] << 8;
      uint16_t hw2 = bytecode[pos++];
      hw2 |= bytecode[pos++] << 8;
      uint16_t hw3 = bytecode[pos++];
      hw3 |= bytecode[pos++] << 8;
      mcode[mcode_size++] = AARCH64_PUSH_X0_STACK;
      mcode[mcode_size++] = a64_movz_x(0, hw0, 0);
      if (hw1)
        mcode[mcode_size++] = a64_movk_x(0, hw1, 16);
      if (hw2)
        mcode[mcode_size++] = a64_movk_x(0, hw2, 32);
      if (hw3)
        mcode[mcode_size++] = a64_movk_x(0, hw3, 48);
    } break;
    case ONDA_OP_ADD:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_ADD_X0_X1_X0;
      break;
    case ONDA_OP_SUB:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_SUB_X0_X1_X0;
      break;
    case ONDA_OP_MUL:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_MUL_X0_X1_X0;
      break;
    case ONDA_OP_DIV:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_UDIV_X0_X1_X0;
      break;
    case ONDA_OP_MOD:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_UDIV_X2_X1_X0;
      mcode[mcode_size++] = AARCH64_MSUB_X0_X2_X0_X1;
      break;
    case ONDA_OP_INC:
      mcode[mcode_size++] = AARCH64_INC_X0;
      break;
    case ONDA_OP_DEC:
      mcode[mcode_size++] = AARCH64_DEC_X0;
      break;
    case ONDA_OP_AND:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      break;
    case ONDA_OP_OR:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_ORR_X0_X1_X0;
      break;
    case ONDA_OP_NOT:
      mcode[mcode_size++] = AARCH64_NOT_X0_X0;
      break;
    case ONDA_OP_EQ:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_EQ;
      break;
    case ONDA_OP_NEQ:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_NE;
      break;
    case ONDA_OP_LT:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_LT;
      break;
    case ONDA_OP_GT:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_GT;
      break;
    case ONDA_OP_LTE:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_LTE;
      break;
    case ONDA_OP_GTE:
      mcode[mcode_size++] = AARCH64_POP_X1_STACK;
      mcode[mcode_size++] = AARCH64_CMP_X0_X1;
      mcode[mcode_size++] = AARCH64_CSET_X0_GTE;
      break;
    case ONDA_OP_SWAP:
      mcode[mcode_size++] = AARCH64_LOAD_X1_STACK;
      mcode[mcode_size++] = AARCH64_STORE_X0_STACK;
      mcode[mcode_size++] = AARCH64_MOV_X0_X1;
      break;
    case ONDA_OP_DUP:
      mcode[mcode_size++] = AARCH64_PUSH_X0_STACK;
      break;
    case ONDA_OP_OVER:
      mcode[mcode_size++] = AARCH64_LOAD_X1_STACK;
      mcode[mcode_size++] = AARCH64_PUSH_X0_STACK;
      mcode[mcode_size++] = AARCH64_MOV_X0_X1;
      break;
    case ONDA_OP_ROT:
      mcode[mcode_size++] = AARCH64_LOAD_X1_STACK;
      mcode[mcode_size++] = 0xF94007E2; // ldr x2, [sp, #8]
      mcode[mcode_size++] = 0xF90007E1; // str x1, [sp, #8]
      mcode[mcode_size++] = AARCH64_STORE_X0_STACK;
      mcode[mcode_size++] = AARCH64_MOV_X0_X2;
    case ONDA_OP_DROP:
      mcode[mcode_size++] = AARCH64_POP_X0_STACK;
      break;
    case ONDA_OP_PRINT: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_u64;
      mcode[mcode_size++] = a64_movz_x(16, (addr >> 0) & 0xFFFF, 0);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 16) & 0xFFFF, 16);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 32) & 0xFFFF, 32);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 48) & 0xFFFF, 48);
      mcode[mcode_size++] = 0xD63F0200; // blr x16
    } break;
    case ONDA_OP_PRINT_STR: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_string;
      mcode[mcode_size++] = a64_movz_x(16, (addr >> 0) & 0xFFFF, 0);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 16) & 0xFFFF, 16);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 32) & 0xFFFF, 32);
      mcode[mcode_size++] = a64_movk_x(16, (addr >> 48) & 0xFFFF, 48);
      mcode[mcode_size++] = 0xD63F0200; // blr x16
    } break;
    case ONDA_OP_HALT:
      mcode[mcode_size++] = AARCH64_RET;
      break;
    default:
      printf("Error: Unknown opcode %02X\n", opcode);
      mcode[mcode_size++] = 0x0BADC0DE; // invalid opcode
      break;
    }
  }

  *out_machine_code = (uint8_t*)mcode;
  *out_machine_code_size = mcode_size * sizeof(uint32_t);

  return 0;
}
