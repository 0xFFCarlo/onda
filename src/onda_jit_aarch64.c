#if defined(__aarch64__)

#include "onda_jit_aarch64.h"

#include "onda_compiler.h"
#include "onda_std.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define DS_REG                  19 // x19 used as data stack pointer (DS)
#define FS_REG                  20 // x20 used as frame stack pointer (FS)
#define ONDA_LOCAL_REG_COUNT    8
#define ONDA_MCODE_INIT_CAP     512
#define ONDA_MAX_OP_INSTR_COUNT 6 // max instructions per opcode

#define AA64_PUSH_X0_STACK  (0xF81F8E60u)            // str x0, [x19, #-8]!
#define AA64_POP_STACK(x)   (0xF8408660u | ((x)&31)) // ldr xN, [x19], #8
#define AA64_STORE_X0_STACK 0xF9000260u              // str x0, [x19]
#define AA64_LOAD_STACK(x)  (0xF9400260u | ((x)&31)) // ldr xN, [x19]
#define AA64_BLR(reg)       (0xD63F0000u | (((reg)&31u) << 5)) // blr xN
#define AA64_B(imm26)       (0x14000000u | ((uint32_t)(imm26)&0x03FFFFFFu))
#define AA64_CBZ(reg, imm19)                                                   \
  (0xB4000000u | ((((uint32_t)(imm19)) & 0x7FFFFu) << 5) | ((reg)&31u))
#define AA64_RET             (0xD65F03C0)                 // ret
#define AA64_MOV(x, y)       (0xAA0003E0 | (y << 16) | x) // mov xX, xY
#define AA64_ADD(dst, a, b)  (0x8B000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_SUB(dst, a, b)  (0xCB000000 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_MUL(dst, a, b)  (0x9B007C00 | ((b) << 16) | ((a) << 5) | dst)
#define AA64_SDIV(dst, a, b) (0x9AC00C00 | ((b) << 16) | ((a) << 5) | dst)
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
#define AA64_CSET_NE(rd)    (0x9A9F07E0 | ((rd)&31u)) // cset xD, ne
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
// 8-bit: STRB/LDRB (offset in bytes, no alignment requirement)
#define AA64_STRBU(rt, rn, off_bytes)                                          \
  (0x39000000u | (((off_bytes)&0xFFFu) << 10) | (((rn)&31u) << 5) | ((rt)&31u))
#define AA64_LDRBU(rt, rn, off_bytes)                                          \
  (0x39400000u | (((off_bytes)&0xFFFu) << 10) | (((rn)&31u) << 5) | ((rt)&31u))

// 16-bit: STRH/LDRH (offset in bytes, must be multiple of 2)
#define AA64_STRHU(rt, rn, off_bytes)                                          \
  (0x79000000u | ((((off_bytes) >> 1) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))
#define AA64_LDRHU(rt, rn, off_bytes)                                          \
  (0x79400000u | ((((off_bytes) >> 1) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))

// 32-bit: STR/LDR (Wt) (offset in bytes, must be multiple of 4)
#define AA64_STRWU(rt, rn, off_bytes)                                          \
  (0xB9000000u | ((((off_bytes) >> 2) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))
#define AA64_LDRWU(rt, rn, off_bytes)                                          \
  (0xB9400000u | ((((off_bytes) >> 2) & 0xFFFu) << 10) | (((rn)&31u) << 5) |   \
   ((rt)&31u))

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
#define AA64_LOCAL_REG(i) (9u + (uint32_t)(i))

#if ONDA_LOCAL_REG_COUNT > 8
#error "ONDA_LOCAL_REG_COUNT must be <= 8 on aarch64 (x9..x16)"
#endif

typedef struct onda_unresolved_jump_t {
  size_t mcode_pos;
  size_t bcode_pos;
  int16_t bcode_jmp_offset;
  struct onda_unresolved_jump_t* next;
  uint8_t jump_type;
} onda_unresolved_jump_t;

size_t onda_jit_aarch64(const onda_runtime_t* rt,
                        uint8_t** out_machine_code,
                        size_t* out_machine_code_size) {
  const uint8_t* bytecode = rt->code;
  const size_t bytecode_entry_pc = rt->entry_pc;
  const size_t bytecode_size = rt->code_size;
  int64_t* data_sp = rt->data_sp;
  int64_t* frame_bp = rt->frame_bp;
  const onda_native_registry_t* reg = rt->native_registry;
  size_t bcode_pos = 0;
  uint32_t* mcode = onda_malloc(ONDA_MCODE_INIT_CAP * sizeof(uint32_t));
  int32_t* bcode_to_mcode = onda_malloc(bytecode_size * sizeof(int32_t));
  size_t mcode_capacity = ONDA_MCODE_INIT_CAP;
  size_t mcode_size = 0;
  uint16_t lo0, hi0, lo1, hi1;
  int16_t jmp_offset;
  onda_unresolved_jump_t* unresolved_jumps = NULL;

  memset(bcode_to_mcode, -1, bytecode_size * sizeof(int32_t));

#define EMIT(a)                                                                \
  do {                                                                         \
    if (mcode_size >= mcode_capacity) {                                        \
      mcode_capacity *= 2;                                                     \
      mcode = onda_realloc(mcode, mcode_capacity * sizeof(uint32_t));          \
    }                                                                          \
    mcode[mcode_size++] = (a);                                                 \
  } while (0)
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

  // Prologue
  // x20 = immediate(frame_bp - (2 + ONDA_LOCAL_REG_COUNT))
  {
    const uint64_t fb =
        (uint64_t)(uintptr_t)(frame_bp - (2 + ONDA_LOCAL_REG_COUNT));
    EMIT(AA64_MOVZ(FS_REG, (fb >> 0) & 0xFFFF, 0));
    EMIT(AA64_MOVK(FS_REG, (fb >> 16) & 0xFFFF, 16));
    EMIT(AA64_MOVK(FS_REG, (fb >> 32) & 0xFFFF, 32));
    EMIT(AA64_MOVK(FS_REG, (fb >> 48) & 0xFFFF, 48));
  }
  // x19 = data stack pointer
  {
    const uint64_t dsp = (uint64_t)(uintptr_t)data_sp;
    EMIT(AA64_MOVZ(DS_REG, (dsp >> 0) & 0xFFFF, 0));
    EMIT(AA64_MOVK(DS_REG, (dsp >> 16) & 0xFFFF, 16));
    EMIT(AA64_MOVK(DS_REG, (dsp >> 32) & 0xFFFF, 32));
    EMIT(AA64_MOVK(DS_REG, (dsp >> 48) & 0xFFFF, 48));
  }
  EMIT(AA64_STRU(30, FS_REG, 0)); // x30 (lr) -> frame[0] = return address
  EMIT(AA64_STRU(31, FS_REG, 8)); // xzr -> frame[1] = 0

  // Jump to start of program
  if (bytecode_entry_pc > 0)
    EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP, bytecode_entry_pc, 0x14000000);

  while (bcode_pos < bytecode_size) {

    // Ensure capacity
    if (mcode_size + ONDA_MAX_OP_INSTR_COUNT >= mcode_capacity) {
      mcode_capacity *= 2;
      mcode = onda_realloc(mcode, mcode_capacity * sizeof(uint32_t));
    }

    // Store Bytecode to machine code mapping, for patching jumps later
    bcode_to_mcode[bcode_pos] = mcode_size;

    // Resolve previously unresolved jumps to this bytecode position
    onda_unresolved_jump_t** link = &unresolved_jumps;
    while (*link) {
      onda_unresolved_jump_t* jmp = *link;

      const size_t target_bpos = jmp->bcode_pos + jmp->bcode_jmp_offset;
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
      EMIT(AA64_PUSH_X0_STACK);
      if (local_id >= ONDA_LOCALS_BASE_OFF &&
          (local_id - ONDA_LOCALS_BASE_OFF) < ONDA_LOCAL_REG_COUNT) {
        EMIT(AA64_MOV(0, AA64_LOCAL_REG(local_id - ONDA_LOCALS_BASE_OFF)));
      } else {
        uint32_t off = (uint32_t)local_id * 8u;
        if (local_id >= ONDA_LOCALS_BASE_OFF) {
          off += (uint32_t)ONDA_LOCAL_REG_COUNT * 8u;
        }
        EMIT(AA64_LDRU(0, FS_REG, off));
      }
    } break;
    case ONDA_OP_STORE_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      if (local_id >= ONDA_LOCALS_BASE_OFF &&
          (local_id - ONDA_LOCALS_BASE_OFF) < ONDA_LOCAL_REG_COUNT) {
        EMIT(AA64_MOV(AA64_LOCAL_REG(local_id - ONDA_LOCALS_BASE_OFF), 0));
      } else {
        uint32_t off = (uint32_t)local_id * 8u;
        if (local_id >= ONDA_LOCALS_BASE_OFF) {
          off += (uint32_t)ONDA_LOCAL_REG_COUNT * 8u;
        }
        EMIT(AA64_STRU(0, FS_REG, off));
      }
      EMIT(AA64_POP_STACK(0));
    } break;
    case ONDA_OP_PUSH_FROM_ADDR_B:
      EMIT(AA64_LDRBU(0, 0, 0)); // x0 = *(uint8_t*)x0
      break;
    case ONDA_OP_STORE_TO_ADDR_B:
      EMIT(AA64_LDRBU(7, DS_REG, 0));      // x7 = value from [ds]
      EMIT(AA64_STRBU(7, 0, 0));           // *(uint8_t*)x0 = x7
      EMIT(AA64_LDRU(0, DS_REG, 8));       // x0 = next item below value
      EMIT(AA64_ADDI(DS_REG, DS_REG, 16)); // ds += 16
      break;
    case ONDA_OP_PUSH_FROM_ADDR_HW:
      EMIT(AA64_LDRHU(0, 0, 0)); // x0 = *(uint16_t*)x0
      break;
    case ONDA_OP_STORE_TO_ADDR_HW:
      EMIT(AA64_LDRHU(7, DS_REG, 0));      // x7 = value from [ds]
      EMIT(AA64_STRHU(7, 0, 0));           // *(uint16_t*)x0 = x7
      EMIT(AA64_LDRU(0, DS_REG, 8));       // x0 = next item below value
      EMIT(AA64_ADDI(DS_REG, DS_REG, 16)); // ds += 32
      break;
    case ONDA_OP_PUSH_FROM_ADDR_W:
      EMIT(AA64_LDRWU(0, 0, 0)); // x0 = *(uint32_t*)x0
      break;
    case ONDA_OP_STORE_TO_ADDR_W:
      EMIT(AA64_LDRWU(7, DS_REG, 0));      // x7 = value from [ds]
      EMIT(AA64_STRWU(7, 0, 0));           // *(uint32_t*)x0 = x7
      EMIT(AA64_LDRU(0, DS_REG, 8));       // x0 = next item below value
      EMIT(AA64_ADDI(DS_REG, DS_REG, 16)); // ds += 32
      break;
    case ONDA_OP_PUSH_FROM_ADDR_DW:
      EMIT(AA64_LDRU(0, 0, 0)); // x0 = *(uint64_t*)x0
      break;
    case ONDA_OP_STORE_TO_ADDR_DW:
      EMIT(AA64_LDRU(7, DS_REG, 0));       // x7 = value from [ds]
      EMIT(AA64_STRU(7, 0, 0));            // *(uint64_t*)x0 = x7
      EMIT(AA64_LDRU(0, DS_REG, 8));       // x0 = next item below value
      EMIT(AA64_ADDI(DS_REG, DS_REG, 16)); // ds += 32
      break;
    case ONDA_OP_ADD:
      EMIT2(AA64_POP_STACK(1), AA64_ADD(0, 1, 0));
      break;
    case ONDA_OP_ADD_CONST_I8: {
      const int8_t imm8 = (int8_t)bytecode[bcode_pos++];
      if (imm8 >= 0) {
        EMIT(AA64_ADDI(0, 0, (uint16_t)imm8));
      } else {
        EMIT(AA64_SUBI(0, 0, (uint16_t)(-imm8)));
      }
    } break;
    case ONDA_OP_SUB:
      EMIT2(AA64_POP_STACK(1), AA64_SUB(0, 1, 0));
      break;
    case ONDA_OP_MUL:
      EMIT2(AA64_POP_STACK(1), AA64_MUL(0, 1, 0));
      break;
    case ONDA_OP_MUL_CONST_I8: {
      const int8_t imm8 = (int8_t)bytecode[bcode_pos++];
      if (imm8 >= 0) {
        EMIT(AA64_MOVZ(1, (uint16_t)imm8, 0));
      } else {
        EMIT(AA64_MOVZ(1, (uint16_t)(-imm8), 0));
        EMIT(AA64_SUB(1, 31, 1));
      }
      EMIT(AA64_MUL(0, 0, 1));
    } break;
    case ONDA_OP_DIV:
      EMIT2(AA64_POP_STACK(1), AA64_SDIV(0, 1, 0));
      break;
    case ONDA_OP_MOD:
      EMIT3(AA64_POP_STACK(1), AA64_SDIV(2, 1, 0), AA64_MSUB(0, 2, 0, 1));
      break;
    case ONDA_OP_AND:
      EMIT(AA64_POP_STACK(1));
      EMIT(AA64_CMP(1, 31));  // x1 != 0
      EMIT(AA64_CSET_NE(1));  // x1 = x1 != 0
      EMIT(AA64_CMP_X0_0);    // x0 != 0
      EMIT(AA64_CSET_X0_NE);  // x0 = x0 != 0
      EMIT(AA64_AND(0, 1, 0)); // x0 = x1 && x0
      break;
    case ONDA_OP_OR:
      EMIT(AA64_POP_STACK(1));
      EMIT(AA64_CMP(1, 31));  // x1 != 0
      EMIT(AA64_CSET_NE(1));  // x1 = x1 != 0
      EMIT(AA64_CMP_X0_0);    // x0 != 0
      EMIT(AA64_CSET_X0_NE);  // x0 = x0 != 0
      EMIT(AA64_ORR(0, 1, 0)); // x0 = x1 || x0
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
    case ONDA_OP_INC_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      if (local_id >= ONDA_LOCALS_BASE_OFF &&
          (local_id - ONDA_LOCALS_BASE_OFF) < ONDA_LOCAL_REG_COUNT) {
        const uint32_t reg = AA64_LOCAL_REG(local_id - ONDA_LOCALS_BASE_OFF);
        EMIT(AA64_ADDI(reg, reg, 1));
      } else {
        uint32_t off = (uint32_t)local_id * 8u;
        if (local_id >= ONDA_LOCALS_BASE_OFF) {
          off += (uint32_t)ONDA_LOCAL_REG_COUNT * 8u;
        }
        EMIT(AA64_LDRU(7, FS_REG, off));
        EMIT(AA64_ADDI(7, 7, 1));
        EMIT(AA64_STRU(7, FS_REG, off));
      }
    } break;
    case ONDA_OP_DEC_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      if (local_id >= ONDA_LOCALS_BASE_OFF &&
          (local_id - ONDA_LOCALS_BASE_OFF) < ONDA_LOCAL_REG_COUNT) {
        const uint32_t reg = AA64_LOCAL_REG(local_id - ONDA_LOCALS_BASE_OFF);
        EMIT(AA64_SUBI(reg, reg, 1));
      } else {
        uint32_t off = (uint32_t)local_id * 8u;
        if (local_id >= ONDA_LOCALS_BASE_OFF) {
          off += (uint32_t)ONDA_LOCAL_REG_COUNT * 8u;
        }
        EMIT(AA64_LDRU(7, FS_REG, off));
        EMIT(AA64_SUBI(7, 7, 1));
        EMIT(AA64_STRU(7, FS_REG, off));
      }
    } break;
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
      EMIT(AA64_STRU(0, DS_REG, 8)); // str x0, [ds, #8]
      EMIT(AA64_STRU(2, DS_REG, 0)); // str x2, [ds, #0]
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
    case ONDA_OP_CALL_NATIVE: {
      for (uint8_t i = 0; i < ONDA_LOCAL_REG_COUNT; i++) {
        EMIT(AA64_STRU(AA64_LOCAL_REG(i), FS_REG, (2 + i) * 8));
      }
      // push TOS to stack first to free x0
      EMIT(AA64_PUSH_X0_STACK);
      // put ds in x0 as argument to native function
      EMIT(AA64_MOV(0, DS_REG));
      // load native function address into x3 and call
      uint32_t idx; memcpy(&idx, &bytecode[bcode_pos], sizeof(uint32_t));
      bcode_pos += sizeof(uint32_t);
      uint64_t fn_addr = (uint64_t)(uintptr_t)reg->items[idx].fn;
      EMIT(AA64_MOVZ(3, (fn_addr >> 0) & 0xFFFF, 0));
      EMIT(AA64_MOVK(3, (fn_addr >> 16) & 0xFFFF, 16));
      EMIT(AA64_MOVK(3, (fn_addr >> 32) & 0xFFFF, 32));
      EMIT(AA64_MOVK(3, (fn_addr >> 48) & 0xFFFF, 48));
      EMIT(AA64_BLR(3));
      // After return, x0 has the new ds, so restore it to ds
      EMIT(AA64_MOV(DS_REG, 0)); // ds = x0
      // Pop ToS to x0 as return value from CALL_NATIVE
      EMIT(AA64_POP_STACK(0));
      for (uint8_t i = 0; i < ONDA_LOCAL_REG_COUNT; i++) {
        EMIT(AA64_LDRU(AA64_LOCAL_REG(i), FS_REG, (2 + i) * 8));
      }
    } break;
    case ONDA_OP_CALL: {
      const uint8_t argc = bytecode[bcode_pos++];
      const uint8_t locals = bytecode[bcode_pos++];
      int32_t branch_offset;
      memcpy(&branch_offset, &bytecode[bcode_pos], 4);
      bcode_pos += sizeof(int32_t);
      // - x6 store x20 (frame_bp)
      // - x20 make room for 2 + locals elements
      // - load arguments from data stack and store to frame stack
      // - pop arguments from data stack
      // - x20[1] store x6 (prev frame_bp)
      // - x20[0] store return address in bytes
      // - branch to function
      EMIT(AA64_MOV(6, FS_REG));
      const uint32_t frame_bytes =
          (uint32_t)(2u + ONDA_LOCAL_REG_COUNT + locals) * 8u;
      if (frame_bytes <= 4095) {
        EMIT(AA64_SUBI(FS_REG, FS_REG, frame_bytes));
      } else {
        printf("Error: frame too large for SUBI immediate\n");
        return -1;
      }

      for (uint8_t i = 0; i < ONDA_LOCAL_REG_COUNT; i++) {
        EMIT(AA64_STRU(AA64_LOCAL_REG(i), FS_REG, (2 + i) * 8));
      }

      // Move arguments from data stack to frame stack
      for (int i = 0; i < argc; i++) {
        const int ds_j = argc - i - 1; // stack index (0=TOS in x0)
        if (ds_j == 0) {
          if (i < ONDA_LOCAL_REG_COUNT) {
            EMIT(AA64_MOV(AA64_LOCAL_REG(i), 0));
          } else {
            EMIT(AA64_STRU(0, FS_REG, (2 + ONDA_LOCAL_REG_COUNT + i) * 8));
          }
        } else {
          EMIT(AA64_LDRU(7, DS_REG, (ds_j - 1) * 8));
          if (i < ONDA_LOCAL_REG_COUNT) {
            EMIT(AA64_MOV(AA64_LOCAL_REG(i), 7));
          } else {
            EMIT(AA64_STRU(7, FS_REG, (2 + ONDA_LOCAL_REG_COUNT + i) * 8));
          }
        }
      }
      // Pop arguments from data stack
      if (argc > 0) {
        // New TOS is the value below the removed args
        const uint32_t new_tos_off = (uint32_t)(argc - 1u) * 8u;
        const uint32_t pop_bytes = (uint32_t)argc * 8u;

        EMIT(AA64_LDRU(0, DS_REG, new_tos_off));    // x0 = new caller TOS
        EMIT(AA64_ADDI(DS_REG, DS_REG, pop_bytes)); // sp += argc * 16
      }

      EMIT(AA64_STRU(6, FS_REG, 1 * 8));
      // address of next instruction after branch, in bytes
      EMIT(AA64_ADR(6, 3 * 4));
      EMIT(AA64_STRU(6, FS_REG, 0 * 8));
      if (bcode_to_mcode[bcode_pos + branch_offset] == -1) {
        printf("Error: Unresolved jump for CALL at bytecode position %ld\n",
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
      EMIT(AA64_LDRU(30, FS_REG, 0));

      // x6 = frame[1] (previous frame_bp)
      EMIT(AA64_LDRU(6, FS_REG, 8));

      // if x6 == 0, jump to root-return path
      // +N instructions to skip over nested return path
      EMIT(AA64_CBZ(6, ONDA_LOCAL_REG_COUNT + 2));

      // normal nested return:
      for (uint8_t i = 0; i < ONDA_LOCAL_REG_COUNT; i++) {
        EMIT(AA64_LDRU(AA64_LOCAL_REG(i), FS_REG, (2 + i) * 8));
      }
      EMIT(AA64_MOV(FS_REG, 6));
      EMIT(AA64_RET);

      // root return to C which restores SP:
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

#endif // defined(__aarch64__)
