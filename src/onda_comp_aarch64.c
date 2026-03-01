#include "onda_comp_aarch64.h"

#include "onda_std.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ONDA_MCODE_INIT_CAP     512
#define ONDA_MAX_OP_INSTR_COUNT 6 // max instructions per opcode

#define AA64_SAVE_SP_X20    (0x910003F4)     // mov x20, sp
#define AA64_RESTORE_SP_X20 (0x9100029F)     // mov sp, x20
#define AA64_RESTORE_LR_x19 (0xAA1303FE)     // mov x30, x19
#define AA64_PUSH_X0_STACK  (0xF81F0FE0)     // str x0, [sp, #-16]!
#define AA64_POP_STACK(x)   (0xF84107E0 | x) // ldr xX, [sp], #16
#define AA64_STORE_X0_STACK (0xF90003E0)     // str x0, [sp]
#define AA64_LOAD_STACK(x)  (0xF94003E0 | x) // ldr xX, [sp]
#define AA64_BLR_X16        (0xD63F0200)     // blr x16
#define AA64_B(imm26)       (0x14000000u | ((uint32_t)(imm26)&0x03FFFFFFu))
#define AA64_CBZ(reg, imm19)                                                   \
  (0xB4000000u | ((((uint32_t)(imm19)) & 0x7FFFFu) << 5) | ((reg)&31u))
#define AA64_RET             (0xD65F03C0)                 // ret
#define AA64_MOV(x, y)       (0xAA0003E0 | (y << 16) | x) // mov xX, xY
#define AA64_ADD(dst, a, b)  (0x8B000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_SUB(dst, a, b)  (0xCB000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_MUL(dst, a, b)  (0x9B007C00 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_UDIV(dst, a, b) (0x9AC00800 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_MSUB(dst, n, m, a)                                                \
  (0x9B008000 | (((m)&31u) << 16) | (((a)&31u) << 10) | (((n)&31u) << 5) |     \
   ((dst)&31u))
#define AA64_INC_X0         (0x91000400) // add x0, x0, #1
#define AA64_DEC_X0         (0xD1000400) // sub x0, x0, #1
#define AA64_AND(dst, a, b) (0x8A000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_ORR(dst, a, b) (0xAA000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_NOT_X0_X0      (0xAA2003E0) // orn x0, x0, xzr
#define AA64_CMP_X0_0       (0xF100001F) // cmp x0, #0
#define AA64_CMP(n, m)      (0xEB00001Fu | (((m)&31u) << 16) | (((n)&31u) << 5))
#define AA64_CSET_X0_NE     (0x9A9F07E0) // cset x0, ne
#define AA64_CSET_X0_EQ     (0x9A9F17E0) // cset x0, eq
#define AA64_CSET_X0_LT     (0x9A9FA7E0) // cset x0, lt
#define AA64_CSET_X0_GT     (0x9A9FD7E0) // cset x0, gt
#define AA64_CSET_X0_LTE    (0x9A9FC7E0) // cset x0, le
#define AA64_CSET_X0_GTE    (0x9A9FB7E0) // cset x0, ge
// move 16-bit immediate into 64-bit register and zero other bits
#define AA64_MOVZ(dst, imm16, shift)                                           \
  (0xD2800000 | (((shift) / 16) << 21) | ((uint32_t)(imm16) << 5) | (dst))
// move 16-bit immediate into 64-bit register, keeping other bits
#define AA64_MOVK(dst, imm16, shift)                                           \
  (0xF2800000 | (((shift) / 16) << 21) | ((uint32_t)(imm16) << 5) | (dst))
// Add or Subtract 12-bit immediate to register, keeping other bits (shift must
// be 0)
#define AA64_ADDI(dst, src, imm12)                                             \
  (0x91000000u | (((imm12)&0xFFFu) << 10) | (((src)&31u) << 5) | ((dst)&31u))
#define AA64_SUBI(dst, src, imm12)                                             \
  (0xD1000000u | (((imm12)&0xFFFu) << 10) | (((src)&31u) << 5) | ((dst)&31u))
// 64-bit STR/LDR unsigned immediate, offset in BYTES (must be multiple of 8)
#define AA64_STRU(rt, rn, off_bytes)                                           \
  (0xF9000000u | ((((off_bytes) >> 3) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))
#define AA64_LDRU(rt, rn, off_bytes)                                           \
  (0xF9400000u | ((((off_bytes) >> 3) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))
// Put address of nearby instruction into register, in bytes
#define AA64_ADR(rd, imm21)                                                    \
  (0x10000000u | ((((uint32_t)(imm21)) & 0x3u) << 29) |                        \
   (((((uint32_t)(imm21)) >> 2) & 0x7FFFFu) << 5) | ((rd)&31u))

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
                         uint64_t* frame_bp,
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

  // Prologue: save sp, lr and load frame_bp
  EMIT(AA64_SAVE_SP_X20);
  // x21 = immediate(frame_bp - 2)
  {
    const uint64_t fb = (uint64_t)(uintptr_t)(frame_bp - 2);
    EMIT(AA64_MOVZ(21, (fb >> 0) & 0xFFFF, 0));
    EMIT(AA64_MOVK(21, (fb >> 16) & 0xFFFF, 16));
    EMIT(AA64_MOVK(21, (fb >> 32) & 0xFFFF, 32));
    EMIT(AA64_MOVK(21, (fb >> 48) & 0xFFFF, 48));
  }
  EMIT(AA64_STRU(30, 21, 0)); // x30 (lr) -> frame[0] = return address
  EMIT(AA64_STRU(31, 21, 8)); // xzr -> frame[1] = 0

  // Jump to start of program
  if (bytecode_entry_pc > 0)
    EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP, bytecode_entry_pc, 0x14000000);

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
      if (jmp->jump_type == ONDA_OP_JUMP_IF_FALSE) {
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
    case ONDA_OP_PUSH_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT2(AA64_PUSH_X0_STACK, AA64_LDRU(0, 21, local_id * 8));
    } break;
    case ONDA_OP_STORE_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT2(AA64_STRU(0, 21, local_id * 8), AA64_POP_STACK(0));
    } break;
    case ONDA_OP_PUSH_FROM_ADDR_DW:
      EMIT(AA64_LDRU(0, 0, 0)); // x0 = *(uint64_t*)x0
      break;
    case ONDA_OP_STORE_TO_ADDR_DW:
      EMIT(AA64_LDRU(7, 31, 0));   // x7 = value from [sp]
      EMIT(AA64_STRU(7, 0, 0));    // *(uint64_t*)x0 = x7
      EMIT(AA64_LDRU(0, 31, 16));  // x0 = next item below value
      EMIT(AA64_ADDI(31, 31, 32)); // sp += 32
      break;
    // TODO: implement byte and halfword memory access opcodes
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
      EMIT3(AA64_POP_STACK(1), AA64_UDIV(2, 1, 0), AA64_MSUB(0, 2, 0, 1));
      break;
    case ONDA_OP_AND:
      EMIT2(AA64_POP_STACK(1), AA64_AND(0, 1, 0));
      break;
    case ONDA_OP_OR:
      EMIT2(AA64_POP_STACK(1), AA64_ORR(0, 1, 0));
      break;
    case ONDA_OP_EQ:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_EQ);
      break;
    case ONDA_OP_NEQ:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_NE);
      break;
    case ONDA_OP_LT:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_LT);
      break;
    case ONDA_OP_GT:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_GT);
      break;
    case ONDA_OP_LTE:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_LTE);
      break;
    case ONDA_OP_GTE:
      EMIT3(AA64_POP_STACK(1), AA64_CMP(1, 0), AA64_CSET_X0_GTE);
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
    case ONDA_OP_JUMP_IF_FALSE: {
      EMIT(AA64_MOV(1, 0));
      EMIT(AA64_POP_STACK(0));
      memcpy(&jmp_offset, &bytecode[bcode_pos], 2);
      if (bcode_to_mcode[bcode_pos + jmp_offset] == -1) {
        EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP_IF_FALSE,
                             jmp_offset,
                             0xB4000000 | 0x1);
      } else {
        const int32_t aa_jmp_offset =
            bcode_to_mcode[bcode_pos + jmp_offset] - mcode_size;
        const uint32_t imm19 = (uint32_t)(aa_jmp_offset & 0x7FFFF);
        EMIT(0xB5000000 | (imm19 << 5) | 0x1); // cbnz x1, label
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
    case ONDA_OP_MALLOC: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_malloc;
      EMIT(AA64_MOVZ(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(AA64_MOVK(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(AA64_MOVK(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(AA64_MOVK(16, (addr >> 48) & 0xFFFF, 48));
      EMIT(AA64_BLR_X16);
    } break;
    case ONDA_OP_FREE: {
      const uint64_t addr = (uint64_t)(uintptr_t)&onda_free;
      EMIT(AA64_MOVZ(16, (addr >> 0) & 0xFFFF, 0));
      EMIT(AA64_MOVK(16, (addr >> 16) & 0xFFFF, 16));
      EMIT(AA64_MOVK(16, (addr >> 32) & 0xFFFF, 32));
      EMIT(AA64_MOVK(16, (addr >> 48) & 0xFFFF, 48));
      EMIT2(AA64_BLR_X16, AA64_POP_STACK(0));
    } break;
    case ONDA_OP_CALL: {
      const uint8_t argc = bytecode[bcode_pos++];
      const uint8_t locals = bytecode[bcode_pos++];
      int32_t branch_offset;
      memcpy(&branch_offset, &bytecode[bcode_pos], 4);
      bcode_pos += sizeof(int32_t);
      // - x6 store x21 (frame_bp)
      // - x21 make room for 2 + locals elements
      // - load arguments from data stack and store to frame stack
      // - pop arguments from data stack
      // - x21[1] store x6 (prev frame_bp)
      // - x21[0] store return address in bytes
      // - branch to function
      EMIT(AA64_MOV(6, 21));
      const uint32_t frame_bytes = (uint32_t)(2u + locals) * 8u;
      if (frame_bytes <= 4095) {
        EMIT(AA64_SUBI(21, 21, frame_bytes));
      } else {
        printf("Error: frame too large for SUBI immediate\n");
        return -1;
      }

      // Move arguments from data stack to frame stack
      for (int i = 0; i < argc; i++) {
        if (i == 0) { // TOS is in x0 already
          EMIT(AA64_STRU(0, 21, (2 + argc - 1 - i) * 8));
        } else {
          // load argument from data stack
          EMIT(AA64_LDRU(7, 31, (i - 1) * 16));
          // store argument to frame stack
          EMIT(AA64_STRU(7, 21, (2 + argc - 1 - i) * 8));
        }
      }
      // Pop arguments from data stack
      if (argc > 0) {
        // New TOS is the value below the removed args
        const uint32_t new_tos_off = (uint32_t)(argc - 1u) * 16u;
        const uint32_t pop_bytes = (uint32_t)argc * 16u;

        EMIT(AA64_LDRU(0, 31, new_tos_off)); // x0 = new caller TOS
        EMIT(AA64_ADDI(31, 31, pop_bytes));  // sp += argc * 16
      }

      EMIT(AA64_STRU(6, 21, 1 * 8));
      // address of next instruction after branch, in bytes
      EMIT(AA64_ADR(6, 3 * 4));
      EMIT(AA64_STRU(6, 21, 0 * 8));
      if (bcode_to_mcode[bcode_pos + branch_offset] == -1) {
        printf("Error: Unresolved jump for CALL at bytecode position %d\n",
               bcode_pos);
      } else {
        const int32_t aa_jmp_offset =
            bcode_to_mcode[bcode_pos + branch_offset] - mcode_size;
        EMIT(AA64_B(aa_jmp_offset));
      }

      break;
    }
    case ONDA_OP_RET:
      // x30 = frame[0] (return address)
      EMIT(AA64_LDRU(30, 21, 0));

      // x6 = frame[1] (previous frame_bp)
      EMIT(AA64_LDRU(6, 21, 8));

      // if x6 == 0, jump to root-return path
      // +3 instructions to skip over the normal nested return path
      EMIT(AA64_CBZ(6, 3));

      // normal nested return:
      EMIT(AA64_MOV(21, 6));
      EMIT(AA64_RET);

      // root return to C which restores SP:
      EMIT(AA64_RESTORE_SP_X20); // mov sp, x20
      EMIT(AA64_RET);
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
