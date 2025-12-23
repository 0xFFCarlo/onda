#include "onda_comp_aarch64.h"

#include "onda_std.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <stdio.h>
#include <string.h>

#define ONDA_MCODE_INIT_CAP     512
#define ONDA_MAX_OP_INSTR_COUNT 6 // max instructions per opcode

#define AA64_SAVE_SP_X20      0x910003F4 // mov x20, sp
#define AA64_SAVE_LR_x19      0xAA1E03F3 // mov x19, x30
#define AA64_RESTORE_SP_X20   0x9100029F // mov sp, x20
#define AA64_RESTORE_LR_x19   0xAA1303FE // mov x30, x19
#define AA64_PUSH_X0_STACK    0xF81F0FE0 // str x0, [sp, #-16]!
#define AA64_POP_X0_STACK     0xF84107E0 // ldr x0, [sp], #16
#define AA64_POP_X1_STACK     0xF84107E1 // ldr x1, [sp], #16
#define AA64_STORE_X0_STACK   0xF90003E0 // str x0, [sp]
#define AA64_LOAD_X1_STACK    0xF94003E1 // ldr x1, [sp]
#define AA64_BLR_X16          0xD63F0200 // blr x16
#define AA64_RET              0xD65F03C0 // ret
#define AA64_MOV_X0_X1        0xAA0103E0 // mov x0, x1
#define AA64_MOV_X0_X2        0xAA0203E0 // mov x0, x2
#define AA64_ADD_X0_X1_X0     0x8B000020 // add x0, x1, x0
#define AA64_SUB_X0_X1_X0     0xCB000020 // sub x0, x1, x0
#define AA64_MUL_X0_X1_X0     0x9B007C20 // mul x0, x1, x0
#define AA64_UDIV_X0_X1_X0    0x9AC00820 // udiv x0, x1, x0
#define AA64_UDIV_X2_X1_X0    0x9AC00822 // udiv x2, x1, x0
#define AA64_MSUB_X0_X2_X0_X1 0x9B008440 // msub x0, x2, x0, x1
#define AA64_INC_X0           0x91000400 // add x0, x0, #1
#define AA64_DEC_X0           0xD1000400 // sub x0, x0, #1
#define AA64_AND_X0_X1_X0     0x8A000020 // and x0, x1, x0
#define AA64_ORR_X0_X1_X0     0xAA000020 // orr x0, x1, x0
#define AA64_NOT_X0_X0        0xAA2003E0 // orn x0, x0, xzr
#define AA64_CMP_X0_X1        0xEB00003F // cmp x0, x1
#define AA64_CSET_X0_EQ       0xE000003F // cset x0, eq
#define AA64_CSET_X0_NE       0x9A9F17E0 // cset x0, ne
#define AA64_CSET_X0_LT       0x9A9FA7E0 // cset x0, lt
#define AA64_CSET_X0_GT       0x9A9F97E0 // cset x0, gt
#define AA64_CSET_X0_LTE      0x9A9FB7E0 // cset x0, le
#define AA64_CSET_X0_GTE      0x9A9F87E0 // cset x0, ge

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
  uint32_t* mcode = onda_malloc(ONDA_MCODE_INIT_CAP * sizeof(uint32_t));
  int32_t* bcode_to_mcode = onda_malloc(bytecode_size * sizeof(int32_t));
  uint32_t* jmp_patch_list = NULL;
  size_t jmp_patch_count = 0;
  size_t mcode_capacity = ONDA_MCODE_INIT_CAP;
  size_t mcode_size = 0;
  uint16_t lo0, hi0, lo1, hi1;
  uint32_t jmp_target;

  memset(bcode_to_mcode, -1, bytecode_size * sizeof(int32_t));

#define EMIT(a) mcode[mcode_size++] = (a)
#define EMIT2(a, b)                                                            \
  EMIT(a);                                                                     \
  EMIT(b)
#define EMIT3(a, b, c)                                                         \
  EMIT2(a, b);                                                                 \
  EMIT(c)

  // Prologue: save lr and sp
  EMIT2(AA64_SAVE_SP_X20, AA64_SAVE_LR_x19);

  while (pos < bytecode_size) {

    // Ensure capacity
    if (mcode_size + ONDA_MAX_OP_INSTR_COUNT >= mcode_capacity) {
      mcode_capacity *= 2;
      mcode = onda_realloc(mcode, mcode_capacity);
    }

    // Store Bytecode to machine code mapping, for patching jumps later
    bcode_to_mcode[pos] = mcode_size;

    const uint8_t opcode = bytecode[pos++];
    switch (opcode) {
    case ONDA_OP_PUSH_CONST_U8:
      EMIT2(AA64_PUSH_X0_STACK, a64_movz_x(0, bytecode[pos++], 0));
      break;
    case ONDA_OP_PUSH_CONST_U32: {
      memcpy(&lo0, &bytecode[pos], 2);
      memcpy(&hi0, &bytecode[pos + 2], 2);
      pos += 4;
      EMIT2(AA64_PUSH_X0_STACK, a64_movz_x(0, lo0, 0));
      if (hi0)
        mcode[mcode_size++] = a64_movk_x(0, hi0, 16);
    } break;
    case ONDA_OP_PUSH_CONST_U64: {
      memcpy(&lo0, &bytecode[pos], 2);
      memcpy(&hi0, &bytecode[pos + 2], 2);
      memcpy(&lo1, &bytecode[pos + 4], 2);
      memcpy(&hi1, &bytecode[pos + 6], 2);
      pos += 8;
      EMIT2(AA64_PUSH_X0_STACK, a64_movz_x(0, lo0, 0));
      if (hi0)
        EMIT(a64_movk_x(0, hi0, 16));
      if (lo1)
        EMIT(a64_movk_x(0, lo1, 32));
      if (hi1)
        EMIT(a64_movk_x(0, hi1, 48));
    } break;
    case ONDA_OP_ADD:
      EMIT2(AA64_POP_X1_STACK, AA64_ADD_X0_X1_X0);
      break;
    case ONDA_OP_SUB:
      EMIT2(AA64_POP_X1_STACK, AA64_SUB_X0_X1_X0);
      break;
    case ONDA_OP_MUL:
      EMIT2(AA64_POP_X1_STACK, AA64_MUL_X0_X1_X0);
      break;
    case ONDA_OP_DIV:
      EMIT2(AA64_POP_X1_STACK, AA64_UDIV_X0_X1_X0);
      break;
    case ONDA_OP_MOD:
      EMIT3(AA64_POP_X1_STACK, AA64_UDIV_X0_X1_X0, AA64_MSUB_X0_X2_X0_X1);
      break;
    case ONDA_OP_INC:
      EMIT(AA64_INC_X0);
      break;
    case ONDA_OP_DEC:
      EMIT(AA64_DEC_X0);
      break;
    case ONDA_OP_AND:
      EMIT(AA64_POP_X1_STACK);
      break;
    case ONDA_OP_OR:
      EMIT2(AA64_POP_X1_STACK, AA64_ORR_X0_X1_X0);
      break;
    case ONDA_OP_NOT:
      EMIT(AA64_NOT_X0_X0);
      break;
    case ONDA_OP_EQ:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_EQ);
      break;
    case ONDA_OP_NEQ:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_NE);
      break;
    case ONDA_OP_LT:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_LT);
      break;
    case ONDA_OP_GT:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_GT);
      break;
    case ONDA_OP_LTE:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_LTE);
      break;
    case ONDA_OP_GTE:
      EMIT3(AA64_POP_X1_STACK, AA64_CMP_X0_X1, AA64_CSET_X0_GTE);
      break;
    case ONDA_OP_SWAP:
      EMIT3(AA64_LOAD_X1_STACK, AA64_STORE_X0_STACK, AA64_MOV_X0_X1);
      break;
    case ONDA_OP_DUP:
      EMIT(AA64_PUSH_X0_STACK);
      break;
    case ONDA_OP_OVER:
      EMIT3(AA64_LOAD_X1_STACK, AA64_PUSH_X0_STACK, AA64_MOV_X0_X1);
      break;
    case ONDA_OP_ROT:
      EMIT(AA64_LOAD_X1_STACK);
      EMIT(0xF94007E2); // ldr x2, [sp, #8]
      EMIT(0xF90007E1); // str x1, [sp, #8]
      EMIT2(AA64_STORE_X0_STACK, AA64_MOV_X0_X2);
      break;
    case ONDA_OP_DROP:
      EMIT(AA64_POP_X0_STACK);
      break;
    case ONDA_OP_JUMP: {
      memcpy(&jmp_target, &bytecode[pos], 4);
      int32_t jmp_offset =
          (int32_t)(bcode_to_mcode[jmp_target] - (mcode_size + 1));
      if (bcode_to_mcode[jmp_target] == -1) {
        jmp_patch_list = onda_realloc(jmp_patch_list,
                                      (jmp_patch_count + 1) * sizeof(uint32_t));
        jmp_patch_list[jmp_patch_count++] = pos - 1;
      }
      pos += 4;
      EMIT(0x94000000 | ((uint32_t)jmp_offset & 0x03FFFFFF)); // b jmp_offset
    } break;
    case ONDA_OP_JUMP_IF: {
      memcpy(&jmp_target, &bytecode[pos], 4);
      int32_t jmp_offset =
          (int32_t)(bcode_to_mcode[jmp_target] - (mcode_size + 1));
      if (bcode_to_mcode[jmp_target] == -1) {
        jmp_patch_list = onda_realloc(jmp_patch_list,
                                      (jmp_patch_count + 1) * sizeof(uint32_t));
        jmp_patch_list[jmp_patch_count++] = pos - 1;
      }
      pos += 4;
      EMIT(0x54000000 | ((uint32_t)jmp_offset << 5) | 0x1);
    } break;
    case ONDA_OP_PRINT: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_u64;
      EMIT(a64_movz_x(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(a64_movk_x(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(a64_movk_x(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(a64_movk_x(16, (addr >> 48) & 0xFFFF, 48));
      EMIT2(AA64_BLR_X16, AA64_POP_X0_STACK);
    } break;
    case ONDA_OP_PRINT_STR: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_string;
      EMIT(a64_movz_x(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(a64_movk_x(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(a64_movk_x(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(a64_movk_x(16, (addr >> 48) & 0xFFFF, 48));
      EMIT2(AA64_BLR_X16, AA64_POP_X0_STACK);
    } break;
    case ONDA_OP_HALT:
      EMIT3(AA64_RESTORE_SP_X20, AA64_RESTORE_LR_x19, AA64_RET);
      break;
    default:
      printf("Error: Unknown opcode %02X\n", opcode);
      break;
    }
  }

  *out_machine_code = (uint8_t*)mcode;
  *out_machine_code_size = mcode_size * sizeof(uint32_t);

  return 0;
}
