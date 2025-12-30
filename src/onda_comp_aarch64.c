#include "onda_comp_aarch64.h"

#include "onda_std.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ONDA_MCODE_INIT_CAP     512
#define ONDA_MAX_OP_INSTR_COUNT 6 // max instructions per opcode

#define AA64_SAVE_SP_X20      (0x910003F4)     // mov x20, sp
#define AA64_SAVE_LR_x19      (0xAA1E03F3)     // mov x19, x30
#define AA64_RESTORE_SP_X20   (0x9100029F)     // mov sp, x20
#define AA64_RESTORE_LR_x19   (0xAA1303FE)     // mov x30, x19
#define AA64_PUSH_X0_STACK    (0xF81F0FE0)     // str x0, [sp, #-16]!
#define AA64_POP_STACK(x)     (0xF84107E0 | x) // ldr xX, [sp], #16
#define AA64_STORE_X0_STACK   (0xF90003E0)     // str x0, [sp]
#define AA64_LOAD_STACK(x)    (0xF94003E0 | x) // ldr xX, [sp]
#define AA64_BLR_X16          (0xD63F0200)     // blr x16
#define AA64_RET              (0xD65F03C0)     // ret
#define AA64_MOV(x, y)        (0xAA0003E0 | (y << 16) | x) // mov xX, xY
#define AA64_ADD(dst, a, b)   (0x8B000000 | (b << 16) | (a << 5) | dst)
#define AA64_SUB(dst, a, b)   (0xCB000000 | (b << 16) | (a << 5) | dst)
#define AA64_MUL(dst, a, b)   (0x9B007C00 | (b << 16) | (a << 5) | dst)
#define AA64_UDIV(dst, a, b)  (0x9AC00800 | (b << 16) | (a << 5) | dst)
#define AA64_UDIV_X2_X1_X0    (0x9AC00822) // udiv x2, x1, x0
#define AA64_MSUB_X0_X2_X0_X1 (0x9B008440) // msub x0, x2, x0, x1
#define AA64_INC_X0           (0x91000400) // add x0, x0, #1
#define AA64_DEC_X0           (0xD1000400) // sub x0, x0, #1
#define AA64_AND(dst, a, b)   (0x8A000000 | (b << 16) | (a << 5) | dst)
#define AA64_ORR(dst, a, b)   (0xAA000000 | (b << 16) | (a << 5) | dst)
#define AA64_NOT_X0_X0        (0xAA2003E0) // orn x0, x0, xzr
#define AA64_CMP_X0_0         (0xF100001F) // cmp x0, #0
#define AA64_CMP_X1_X0        (0xEB00003F) // cmp x1, x0
#define AA64_CSET_X0_NE       (0x9A9F07E0) // cset x0, ne
#define AA64_CSET_X0_EQ       (0x9A9F17E0) // cset x0, eq
#define AA64_CSET_X0_LT       (0x9A9FA7E0) // cset x0, lt
#define AA64_CSET_X0_GT       (0x9A9FD7E0) // cset x0, gt
#define AA64_CSET_X0_LTE      (0x9A9FC7E0) // cset x0, le
#define AA64_CSET_X0_GTE      (0x9A9FB7E0) // cset x0, ge
// move 16-bit immediate into 64-bit register and zero other bits
#define AA64_MOVZ(dst, imm16, shift)                                           \
  (0xD2800000 | (((shift) / 16) << 21) | ((uint32_t)(imm16) << 5) | (dst))
// move 16-bit immediate into 64-bit register, keeping other bits
#define AA64_MOVK(dst, imm16, shift)                                           \
  (0xF2800000 | (((shift) / 16) << 21) | ((uint32_t)(imm16) << 5) | (dst))

static inline bool op_consumes_stack(uint8_t opcode) {
  switch (opcode) {
  case ONDA_OP_ADD:
  case ONDA_OP_SUB:
  case ONDA_OP_MUL:
  case ONDA_OP_DIV:
  case ONDA_OP_MOD:
  case ONDA_OP_AND:
  case ONDA_OP_OR:
  case ONDA_OP_EQ:
  case ONDA_OP_NEQ:
  case ONDA_OP_LT:
  case ONDA_OP_GT:
  case ONDA_OP_LTE:
  case ONDA_OP_GTE:
    return true;
  default:
    return false;
  }
}

typedef struct onda_unresolved_jump_t {
  size_t mcode_pos;
  size_t bcode_pos;
  int16_t bcode_jmp_offset;
  struct onda_unresolved_jump_t* next;
  uint8_t jump_type;
} onda_unresolved_jump_t;

size_t onda_comp_aarch64(const uint8_t* bytecode,
                         const size_t bytecode_entry_pc,
                         size_t bytecode_size,
                         uint8_t** out_machine_code,
                         size_t* out_machine_code_size) {
  int bcode_pos = 0;
  uint32_t* mcode = onda_malloc(ONDA_MCODE_INIT_CAP * sizeof(uint32_t));
  int32_t* bcode_to_mcode = onda_malloc(bytecode_size * sizeof(int32_t));
  size_t mcode_capacity = ONDA_MCODE_INIT_CAP;
  size_t mcode_size = 0;
  uint16_t lo0, hi0, lo1, hi1;
  int16_t jmp_offset;
  onda_unresolved_jump_t* unresolved_jumps = NULL;
  bool tos_in_x1 = false;

  memset(bcode_to_mcode, -1, bytecode_size * sizeof(int32_t));

#define EMIT(a) mcode[mcode_size++] = (a)
#define EMIT2(a, b)                                                            \
  EMIT(a);                                                                     \
  EMIT(b)
#define EMIT3(a, b, c)                                                         \
  EMIT2(a, b);                                                                 \
  EMIT(c)

#define EMIT_UNRESOLVED_JUMP(jmp_type, offset, op_placeholder)                 \
  {                                                                            \
    onda_unresolved_jump_t* uj =                                               \
        (onda_unresolved_jump_t*)onda_calloc(1, sizeof(*uj));                  \
    uj->mcode_pos = mcode_size;                                                \
    uj->bcode_pos = bcode_pos;                                                 \
    uj->bcode_jmp_offset = offset;                                             \
    uj->jump_type = jmp_type;                                                  \
    uj->next = unresolved_jumps;                                               \
    unresolved_jumps = uj;                                                     \
    EMIT(op_placeholder);                                                      \
  }

  // Prologue: save lr and sp
  EMIT2(AA64_SAVE_SP_X20, AA64_SAVE_LR_x19);
  if (bytecode_entry_pc > 0) {
    EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP, bytecode_entry_pc, 0x14000000);
    printf("Entry PC jump placeholder at mcode pos %zu\n", mcode_size - 1);
  }

  while (bcode_pos < bytecode_size) {

    // Ensure capacity
    if (mcode_size + ONDA_MAX_OP_INSTR_COUNT >= mcode_capacity) {
      mcode_capacity *= 2;
      mcode = onda_realloc(mcode, mcode_capacity);
    }

    // Store Bytecode to machine code mapping, for patching jumps later
    bcode_to_mcode[bcode_pos] = mcode_size;

    // Resolve previously unresolved jumps to this bytecode position
    onda_unresolved_jump_t** link = &unresolved_jumps;
    while (*link) {
      onda_unresolved_jump_t* jmp = *link;

      const int32_t target_bpos = jmp->bcode_pos + jmp->bcode_jmp_offset;
      if (target_bpos != bcode_pos) {
        link = &jmp->next;
        continue;
      }

      const int32_t aa_jmp_offset = mcode_size - jmp->mcode_pos;
      if (jmp->jump_type == ONDA_OP_JUMP_IF) {
        const uint32_t imm19 = (uint32_t)aa_jmp_offset & 0x7FFFFu;
        const uint32_t imm19_mask = 0x00FFFFE0u; // bits [23:5]
        const uint64_t original = mcode[jmp->mcode_pos];
        mcode[jmp->mcode_pos] = (original & ~imm19_mask) | (imm19 << 5);
      } else {
        const uint32_t imm26 = (uint32_t)aa_jmp_offset & 0x03FFFFFFu;
        const uint32_t imm26_mask = 0x03FFFFFFu; // bits [25:0]
        const uint64_t original = mcode[jmp->mcode_pos];
        mcode[jmp->mcode_pos] = (original & ~imm26_mask) | imm26;
      }

      // remove node
      *link = jmp->next;
      onda_free(jmp);
    }

    const uint8_t opcode = bytecode[bcode_pos++];
    const uint8_t next_opcode =
        (bcode_pos < bytecode_size) ? bytecode[bcode_pos] : 0;
    switch (opcode) {
    case ONDA_OP_PUSH_CONST_U8:
      EMIT2(AA64_PUSH_X0_STACK, AA64_MOVZ(0, bytecode[bcode_pos++], 0));
      break;
    case ONDA_OP_PUSH_CONST_U32: {
      memcpy(&lo0, &bytecode[bcode_pos], 2);
      memcpy(&hi0, &bytecode[bcode_pos + 2], 2);
      bcode_pos += 4;
      EMIT2(AA64_PUSH_X0_STACK, AA64_MOVZ(0, lo0, 0));
      if (hi0)
        mcode[mcode_size++] = AA64_MOVK(0, hi0, 16);
    } break;
    case ONDA_OP_PUSH_CONST_U64: {
      memcpy(&lo0, &bytecode[bcode_pos], 2);
      memcpy(&hi0, &bytecode[bcode_pos + 2], 2);
      memcpy(&lo1, &bytecode[bcode_pos + 4], 2);
      memcpy(&hi1, &bytecode[bcode_pos + 6], 2);
      bcode_pos += 8;
      EMIT2(AA64_PUSH_X0_STACK, AA64_MOVZ(0, lo0, 0));
      if (hi0)
        EMIT(AA64_MOVK(0, hi0, 16));
      if (lo1)
        EMIT(AA64_MOVK(0, lo1, 32));
      if (hi1)
        EMIT(AA64_MOVK(0, hi1, 48));
    } break;
    case ONDA_OP_ADD:
      EMIT2(AA64_POP_STACK(1), AA64_ADD(0, 1, 0));
      break;
    case ONDA_OP_SUB:
      EMIT2(AA64_POP_STACK(1), AA64_SUB(0, 1, 0));
      break;
    case ONDA_OP_MUL:
      EMIT2(AA64_POP_STACK(1), AA64_MUL(0, 1, 0));
      break;
    case ONDA_OP_DIV:
      EMIT2(AA64_POP_STACK(1), AA64_UDIV(0, 1, 0));
      break;
    case ONDA_OP_MOD:
      EMIT3(AA64_POP_STACK(1), AA64_UDIV_X2_X1_X0, AA64_MSUB_X0_X2_X0_X1);
      break;
    case ONDA_OP_AND:
      EMIT2(AA64_POP_STACK(1), AA64_AND(0, 1, 0));
      break;
    case ONDA_OP_OR:
      EMIT2(AA64_POP_STACK(1), AA64_ORR(0, 1, 0));
      break;
    case ONDA_OP_EQ:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_EQ);
      break;
    case ONDA_OP_NEQ:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_NE);
      break;
    case ONDA_OP_LT:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_LT);
      break;
    case ONDA_OP_GT:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_GT);
      break;
    case ONDA_OP_LTE:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_LTE);
      break;
    case ONDA_OP_GTE:
      EMIT3(AA64_POP_STACK(1), AA64_CMP_X1_X0, AA64_CSET_X0_GTE);
      break;
    case ONDA_OP_SWAP:
      EMIT3(AA64_LOAD_STACK(1), AA64_STORE_X0_STACK, AA64_MOV(0, 1));
      break;
    case ONDA_OP_INC:
      EMIT(AA64_INC_X0);
      break;
    case ONDA_OP_DEC:
      EMIT(AA64_DEC_X0);
      break;
    case ONDA_OP_NOT:
      EMIT(AA64_CMP_X0_0);
      EMIT(AA64_CSET_X0_EQ);
      break;
    case ONDA_OP_DUP:
      EMIT(AA64_PUSH_X0_STACK);
      break;
    case ONDA_OP_OVER:
      EMIT3(AA64_LOAD_STACK(1), AA64_PUSH_X0_STACK, AA64_MOV(0, 1));
      break;
    case ONDA_OP_ROT: // (a b c -- c a b)
      EMIT(AA64_LOAD_STACK(1));
      EMIT(AA64_LOAD_STACK(2));
      EMIT(0xF90007E0); // str x0, [sp, #8]
      EMIT(0xF90003E2); // str x2, [sp, #0]
      EMIT(AA64_MOV(0, 1));
      break;
    case ONDA_OP_DROP:
      EMIT(AA64_POP_STACK(0));
      break;
    case ONDA_OP_JUMP: {
      memcpy(&jmp_offset, &bytecode[bcode_pos], 2);
      if (bcode_to_mcode[bcode_pos + jmp_offset] == -1) {
        EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP, jmp_offset, 0x14000000);
      } else {
        const int32_t aa_jmp_offset =
            bcode_to_mcode[bcode_pos + jmp_offset] - mcode_size;
        EMIT(0x14000000 |
             ((uint32_t)aa_jmp_offset & 0x03FFFFFF)); // b jmp_offset
      }
      bcode_pos += 2;
    } break;
    case ONDA_OP_JUMP_IF: {
      EMIT(AA64_MOV(1, 0));
      EMIT(AA64_POP_STACK(0));
      memcpy(&jmp_offset, &bytecode[bcode_pos], 2);
      if (bcode_to_mcode[bcode_pos + jmp_offset] == -1) {
        EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP_IF, jmp_offset, 0xB5000000 | 0x1);
      } else {
        const int32_t aa_jmp_offset =
            bcode_to_mcode[bcode_pos + jmp_offset] - mcode_size;
        const uint32_t imm19 = (uint32_t)(aa_jmp_offset & 0x7FFFF);
        EMIT(0xB5000000 | (imm19 << 5) | 0x1); // cbnz x1, label
      }
      bcode_pos += 2;
    } break;
    case ONDA_OP_DEC_JUMP_IF_NZ: {
      EMIT(AA64_DEC_X0);
      memcpy(&jmp_offset, &bytecode[bcode_pos], 2);
      if (bcode_to_mcode[bcode_pos + jmp_offset] == -1) {
        EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP_IF, jmp_offset, 0xB5000000 | 0x0);
      } else {
        const int32_t aa_jmp_offset =
            bcode_to_mcode[bcode_pos + jmp_offset] - mcode_size;
        const uint32_t imm19 = (uint32_t)(aa_jmp_offset & 0x7FFFF);
        EMIT(0xB5000000 | (imm19 << 5) | 0x0); // cbnz x0, label
      }
      bcode_pos += 2;
    } break;
    case ONDA_OP_PRINT: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_u64;
      EMIT(AA64_MOVZ(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(AA64_MOVK(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(AA64_MOVK(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(AA64_MOVK(16, (addr >> 48) & 0xFFFF, 48));
      EMIT2(AA64_BLR_X16, AA64_POP_STACK(0));
    } break;
    case ONDA_OP_PRINT_STR: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_print_string;
      EMIT(AA64_MOVZ(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(AA64_MOVK(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(AA64_MOVK(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(AA64_MOVK(16, (addr >> 48) & 0xFFFF, 48));
      EMIT2(AA64_BLR_X16, AA64_POP_STACK(0));
    } break;
    case ONDA_OP_RET:
      EMIT3(AA64_RESTORE_SP_X20, AA64_RESTORE_LR_x19, AA64_RET);
      break;
    default:
      printf("Error: Unknown opcode %02X\n", opcode);
      break;
    }
  }

  // Error unresolved jumps
  if (unresolved_jumps) {
    printf("Error: Unresolved jumps remain after compilation.\n");
    // Free unresolved jumps
    onda_unresolved_jump_t* uj = unresolved_jumps;
    while (uj) {
      onda_unresolved_jump_t* next = uj->next;
      onda_free(uj);
      uj = next;
    }
    onda_free(mcode);
    onda_free(bcode_to_mcode);
    return -1;
  }

  *out_machine_code = (uint8_t*)mcode;
  *out_machine_code_size = mcode_size * sizeof(uint32_t);

  return 0;
}
