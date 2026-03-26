#if defined(__x86_64__) && ONDA_ENABLE_JIT_X86_64

#include "onda_jit_x86_64.h"

#include "onda_compiler.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ONDA_MCODE_INIT_CAP    4096
#define ONDA_MAX_OP_BYTE_COUNT 64
#define ONDA_LOCAL_REG_COUNT   3

typedef struct onda_unresolved_jump_t {
  size_t mcode_pos;
  size_t bcode_pos;
  int16_t bcode_jmp_offset;
  struct onda_unresolved_jump_t* next;
  uint8_t jump_type;
} onda_unresolved_jump_t;

int onda_jit_x86_64(const onda_runtime_t* rt,
                    uint8_t** out_machine_code,
                    size_t* out_machine_code_size) {
  const uint8_t* bytecode = rt->code;
  const size_t bytecode_entry_pc = rt->entry_pc;
  const size_t bytecode_size = rt->code_size;
  int64_t* data_sp = rt->data_sp;
  int64_t* frame_bp = rt->frame_bp;
  const onda_native_registry_t* reg = rt->native_registry;
  size_t bcode_pos = 0;
  uint8_t* mcode = onda_malloc(ONDA_MCODE_INIT_CAP);
  int32_t* bcode_to_mcode = onda_malloc(bytecode_size * sizeof(int32_t));
  size_t mcode_capacity = ONDA_MCODE_INIT_CAP;
  size_t mcode_size = 0;
  int16_t jmp_offset;
  onda_unresolved_jump_t* unresolved_jumps = NULL;

  memset(bcode_to_mcode, -1, bytecode_size * sizeof(int32_t));

  static const uint8_t k_local_from_rax_modrm[ONDA_LOCAL_REG_COUNT] = {0xC0, 0xC1, 0xC2};
  static const uint8_t k_rax_from_local_modrm[ONDA_LOCAL_REG_COUNT] = {0xC0, 0xC8, 0xD0};
  static const uint8_t k_rcx_from_local_modrm[ONDA_LOCAL_REG_COUNT] = {0xC1, 0xC9, 0xD1};
  static const uint8_t k_local_from_rdx_modrm[ONDA_LOCAL_REG_COUNT] = {0xD0, 0xD1, 0xD2};
  static const uint8_t k_frame_local_disp8_modrm[ONDA_LOCAL_REG_COUNT] = {0x45, 0x4D, 0x55};
  static const uint8_t k_frame_local_disp32_modrm[ONDA_LOCAL_REG_COUNT] = {0x85, 0x8D, 0x95};
  static const uint8_t k_inc_local_reg_modrm[ONDA_LOCAL_REG_COUNT] = {0xC0, 0xC1, 0xC2};
  static const uint8_t k_dec_local_reg_modrm[ONDA_LOCAL_REG_COUNT] = {0xC8, 0xC9, 0xCA};

#define EMIT(b) mcode[mcode_size++] = (uint8_t)(b)
#define EMITV(...)                                                             \
  do {                                                                         \
    const uint8_t _bytes[] = {__VA_ARGS__};                                    \
    for (size_t _i = 0; _i < sizeof(_bytes); ++_i) {                           \
      EMIT(_bytes[_i]);                                                        \
    }                                                                          \
  } while (0)
#define EMIT_IMM32(val)                                                        \
  do {                                                                         \
    uint32_t _v = (uint32_t)(val);                                             \
    EMITV(_v & 0xFF, (_v >> 8) & 0xFF, (_v >> 16) & 0xFF, (_v >> 24) & 0xFF);  \
  } while (0)
#define EMIT_IMM64(val)                                                        \
  do {                                                                         \
    uint64_t _v = (uint64_t)(val);                                             \
    for (int _i = 0; _i < 8; _i++) {                                           \
      EMIT((uint8_t)(_v >> (_i * 8)));                                         \
    }                                                                          \
  } while (0)

// sub r12, 8 ; mov [r12], rax  -- push rax to data stack
#define EMIT_PUSH_RAX_DS                                                       \
  EMITV(0x49, 0x83, 0xEC, 0x08);                                               \
  EMITV(0x49, 0x89, 0x04, 0x24)
// mov rax, [r12] ; add r12, 8  -- pop data stack to rax
#define EMIT_POP_DS_RAX                                                        \
  EMITV(0x49, 0x8B, 0x04, 0x24);                                               \
  EMITV(0x49, 0x83, 0xC4, 0x08)
// mov rcx, [r12] ; add r12, 8  -- pop data stack to rcx
#define EMIT_POP_DS_RCX                                                        \
  EMITV(0x49, 0x8B, 0x0C, 0x24);                                               \
  EMITV(0x49, 0x83, 0xC4, 0x08)
// mov rcx, [r12]  -- peek data stack to rcx (no pop)
#define EMIT_PEEK_DS_RCX EMITV(0x49, 0x8B, 0x0C, 0x24)
// mov [r12], rax  -- store rax to stack top (no push)
#define EMIT_STORE_RAX_DS EMITV(0x49, 0x89, 0x04, 0x24)

#define IS_FAST_LOCAL(local_id)                                                \
  ((local_id) >= ONDA_LOCALS_BASE_OFF &&                                       \
   ((local_id) - ONDA_LOCALS_BASE_OFF) < ONDA_LOCAL_REG_COUNT)
#define LOCAL_REG_IDX(local_id) ((uint8_t)((local_id) - ONDA_LOCALS_BASE_OFF))
#define READ_BCODE(type, out)                                                  \
  do {                                                                         \
    memcpy(&(out), &bytecode[bcode_pos], sizeof(type));                        \
    bcode_pos += sizeof(type);                                                 \
  } while (0)

// Record unresolved jump; mcode_pos points to the rel32 placeholder slot
#define EMIT_UNRESOLVED_JUMP(jmp_type, jmp_off)                                \
  {                                                                            \
    onda_unresolved_jump_t* uj =                                               \
        (onda_unresolved_jump_t*)onda_calloc(1, sizeof(*uj));                  \
    uj->mcode_pos = mcode_size;                                                \
    uj->bcode_pos = bcode_pos;                                                 \
    uj->bcode_jmp_offset = (jmp_off);                                          \
    uj->jump_type = (jmp_type);                                                \
    uj->next = unresolved_jumps;                                               \
    unresolved_jumps = uj;                                                     \
    EMIT_IMM32(0);                                                             \
  }

#define EMIT_FRAME_OP(op, disp8_modrm, disp32_modrm, off)                      \
  do {                                                                         \
    if ((off) <= 127)                                                          \
      EMITV(0x49, (op), (disp8_modrm), (uint8_t)(off));                        \
    else {                                                                     \
      EMITV(0x49, (op), (disp32_modrm));                                       \
      EMIT_IMM32(off);                                                         \
    }                                                                          \
  } while (0)

// Emit mov r{ax, cx, dx}, [r13 + off] / mov [r13 + off], r{ax, dx}
#define EMIT_LOAD_FRAME(off) EMIT_FRAME_OP(0x8B, 0x45, 0x85, off)
#define EMIT_LOAD_FRAME_RCX(off) EMIT_FRAME_OP(0x8B, 0x4D, 0x8D, off)
#define EMIT_STORE_FRAME_RAX(off) EMIT_FRAME_OP(0x89, 0x45, 0x85, off)
#define EMIT_STORE_FRAME_RDX(off) EMIT_FRAME_OP(0x89, 0x55, 0x95, off)

#define EMIT_LOAD_DS_REG(disp8_modrm, disp32_modrm, off)                       \
  do {                                                                         \
    if ((off) == 0)                                                            \
      EMITV(0x49, 0x8B, (disp8_modrm), 0x24);                                  \
    else if ((off) <= 127) {                                                   \
      EMITV(0x49, 0x8B, (disp32_modrm), 0x24);                                 \
      EMIT((uint8_t)(off));                                                    \
    } else {                                                                   \
      EMITV(0x49, 0x8B, (uint8_t)((disp32_modrm) + 0x40), 0x24);               \
      EMIT_IMM32(off);                                                         \
    }                                                                          \
  } while (0)

// Emit mov r{ax, dx}, [r12 + off] (load from data stack with offset)
#define EMIT_LOAD_DS_RAX(off) EMIT_LOAD_DS_REG(0x04, 0x44, off)
#define EMIT_LOAD_DS_RDX(off) EMIT_LOAD_DS_REG(0x14, 0x54, off)

#define EMIT_MOV_LOCAL_FROM_RAX(idx)                                           \
  do {                                                                         \
    EMITV(0x49, 0x89, k_local_from_rax_modrm[(idx)]);                          \
  } while (0)

#define EMIT_MOV_RAX_FROM_LOCAL(idx)                                           \
  do {                                                                         \
    EMITV(0x4C, 0x89, k_rax_from_local_modrm[(idx)]);                          \
  } while (0)

#define EMIT_MOV_RCX_FROM_LOCAL(idx)                                           \
  do {                                                                         \
    EMITV(0x4C, 0x89, k_rcx_from_local_modrm[(idx)]);                          \
  } while (0)

#define EMIT_MOV_LOCAL_FROM_RDX(idx)                                           \
  do {                                                                         \
    EMITV(0x49, 0x89, k_local_from_rdx_modrm[(idx)]);                          \
  } while (0)

#define EMIT_STORE_FRAME_LOCAL(off, idx)                                       \
  do {                                                                         \
    if ((off) <= 127) {                                                        \
      EMITV(0x4D, 0x89, k_frame_local_disp8_modrm[(idx)], (uint8_t)(off));     \
    } else {                                                                   \
      EMITV(0x4D, 0x89, k_frame_local_disp32_modrm[(idx)]);                    \
      EMIT_IMM32(off);                                                         \
    }                                                                          \
  } while (0)

#define EMIT_LOAD_FRAME_LOCAL(idx, off)                                        \
  do {                                                                         \
    if ((off) <= 127) {                                                        \
      EMITV(0x4D, 0x8B, k_frame_local_disp8_modrm[(idx)], (uint8_t)(off));     \
    } else {                                                                   \
      EMITV(0x4D, 0x8B, k_frame_local_disp32_modrm[(idx)]);                    \
      EMIT_IMM32(off);                                                         \
    }                                                                          \
  } while (0)

#define EMIT_SETCC_FROM_POP_CMP(setcc_byte)                                    \
  do {                                                                         \
    EMIT_POP_DS_RCX;                                                           \
    EMITV(0x48, 0x39, 0xC1);                                                   \
    EMITV(0x0F, (setcc_byte), 0xC0);                                           \
    EMITV(0x0F, 0xB6, 0xC0);                                                   \
  } while (0)

#define EMIT_LOCAL_TO_RCX(local_id)                                            \
  do {                                                                         \
    if (IS_FAST_LOCAL(local_id)) {                                             \
      EMIT_MOV_RCX_FROM_LOCAL(LOCAL_REG_IDX(local_id));                        \
    } else {                                                                   \
      EMIT_LOAD_FRAME_RCX((uint32_t)(local_id) * 8);                           \
    }                                                                          \
  } while (0)

#define EMIT_STORE_LOCAL_FROM_RAX(local_id)                                    \
  do {                                                                         \
    if (IS_FAST_LOCAL(local_id)) {                                             \
      EMIT_MOV_LOCAL_FROM_RAX(LOCAL_REG_IDX(local_id));                        \
    } else {                                                                   \
      EMIT_STORE_FRAME_RAX((uint32_t)(local_id) * 8);                          \
    }                                                                          \
  } while (0)

#define EMIT_LOAD_LOCAL_TO_RAX(local_id)                                       \
  do {                                                                         \
    if (IS_FAST_LOCAL(local_id)) {                                             \
      EMIT_MOV_RAX_FROM_LOCAL(LOCAL_REG_IDX(local_id));                        \
    } else {                                                                   \
      EMIT_LOAD_FRAME((uint32_t)(local_id) * 8);                               \
    }                                                                          \
  } while (0)

#define EMIT_STORE_TO_ADDR(width_prefix, store_opcode)                         \
  do {                                                                         \
    EMITV(0x49, 0x8B, 0x0C, 0x24);                                             \
    if ((width_prefix) != 0) {                                                 \
      EMIT((width_prefix));                                                    \
    }                                                                          \
    EMITV((store_opcode), 0x08);                                               \
    EMITV(0x49, 0x8B, 0x44, 0x24, 0x08);                                       \
    EMITV(0x49, 0x83, 0xC4, 0x10);                                             \
  } while (0)

#define EMIT_JUMP_REL32_OR_UNRESOLVED(jt, joff)                                \
  do {                                                                         \
    const size_t _target_bpos = (size_t)((ssize_t)bcode_pos + (joff));         \
    if (bcode_to_mcode[_target_bpos] == -1) {                                  \
      EMIT_UNRESOLVED_JUMP((jt), (joff));                                      \
    } else {                                                                   \
      const int32_t _rel32 = (int32_t)((int64_t)bcode_to_mcode[_target_bpos] - \
                                       (int64_t)(mcode_size + 4));             \
      EMIT_IMM32((uint32_t)_rel32);                                            \
    }                                                                          \
  } while (0)

#define EMIT_SAVE_LOCAL_REGS                                                    \
  do {                                                                         \
    EMIT_STORE_FRAME_LOCAL((2 + 0) * 8, 0);                                    \
    EMIT_STORE_FRAME_LOCAL((2 + 1) * 8, 1);                                    \
    EMIT_STORE_FRAME_LOCAL((2 + 2) * 8, 2);                                    \
  } while (0)

#define EMIT_RESTORE_LOCAL_REGS                                                 \
  do {                                                                         \
    EMIT_LOAD_FRAME_LOCAL(0, (2 + 0) * 8);                                     \
    EMIT_LOAD_FRAME_LOCAL(1, (2 + 1) * 8);                                     \
    EMIT_LOAD_FRAME_LOCAL(2, (2 + 2) * 8);                                     \
  } while (0)

#define EMIT_ADD_REG_IMM32(reg_imm8_modrm, reg_imm32_modrm, imm)               \
  do {                                                                         \
    if ((imm) <= 127)                                                          \
      EMITV(0x49, 0x83, (reg_imm8_modrm), (uint8_t)(imm));                     \
    else {                                                                     \
      EMITV(0x49, 0x81, (reg_imm32_modrm));                                    \
      EMIT_IMM32(imm);                                                         \
    }                                                                          \
  } while (0)

// Ensure mcode buffer has room for 'n' more bytes
#define ENSURE_CAPACITY(n)                                                     \
  while (mcode_size + (n) >= mcode_capacity) {                                 \
    mcode_capacity *= 2;                                                       \
    mcode = onda_realloc(mcode, mcode_capacity);                               \
  }

  // Prologue
  EMIT(0x55);              // push rbp
  EMITV(0x48, 0x89, 0xE5); // mov rbp, rsp
  EMITV(0x41, 0x55);       // push r13  (FS_REG callee-save)
  EMITV(0x41, 0x54);       // push r12  (DS_REG callee-save)
  // mov r13, frame_bp - (2 + ONDA_LOCAL_REG_COUNT)
  {
    const uint64_t fb =
        (uint64_t)(uintptr_t)(frame_bp - (2 + ONDA_LOCAL_REG_COUNT));
    EMITV(0x49, 0xBD);
    EMIT_IMM64(fb);
  }
  // mov r12, data_sp
  {
    const uint64_t dsp = (uint64_t)(uintptr_t)data_sp;
    EMITV(0x49, 0xBC);
    EMIT_IMM64(dsp);
  }
  EMITV(0x48, 0x8B, 0x45, 0x08); // mov rax, [rbp+8]
  EMITV(0x49, 0x89, 0x45, 0x00); // mov [r13+0], rax
  EMITV(0x31, 0xC0);             // xor eax, eax
  EMITV(0x49, 0x89, 0x45, 0x08); // mov [r13+8], rax

  // Optional forward jump to entry point
  if (bytecode_entry_pc > 0) {
    EMIT(0xE9); // jmp rel32
    EMIT_UNRESOLVED_JUMP(ONDA_OP_JUMP, (int16_t)bytecode_entry_pc);
  }

  while (bcode_pos < bytecode_size) {
    ENSURE_CAPACITY(ONDA_MAX_OP_BYTE_COUNT);

    bcode_to_mcode[bcode_pos] = (int32_t)mcode_size;

    // Resolve any pending jumps targeting this bytecode position
    onda_unresolved_jump_t** link = &unresolved_jumps;
    while (*link) {
      onda_unresolved_jump_t* jmp = *link;
      const size_t target_bpos = jmp->bcode_pos + jmp->bcode_jmp_offset;
      if (target_bpos != bcode_pos) {
        link = &jmp->next;
        continue;
      }
      const int32_t rel32 =
          (int32_t)((int64_t)mcode_size - (int64_t)(jmp->mcode_pos + 4));
      memcpy(&mcode[jmp->mcode_pos], &rel32, 4);
      *link = jmp->next;
      onda_free(jmp);
    }

    const uint8_t opcode = bytecode[bcode_pos++];
    switch (opcode) {

    case ONDA_OP_PUSH_CONST_U8:
      EMIT_PUSH_RAX_DS;
      EMIT(0xB8); // mov eax, imm32
      EMIT_IMM32((uint32_t)bytecode[bcode_pos++]);
      break;

    case ONDA_OP_PUSH_CONST_U32: {
      uint32_t val;
      READ_BCODE(uint32_t, val);
      EMIT_PUSH_RAX_DS;
      EMIT(0xB8); // mov eax, imm32
      EMIT_IMM32(val);
    } break;

    case ONDA_OP_PUSH_CONST_U64: {
      uint64_t val;
      READ_BCODE(uint64_t, val);
      EMIT_PUSH_RAX_DS;
      EMITV(0x48, 0xB8); // mov rax, imm64
      EMIT_IMM64(val);
    } break;
    case ONDA_OP_PUSH_CONST_POOL_PTR_U32: {
      uint32_t offset = 0;
      READ_BCODE(uint32_t, offset);
      if (!rt->const_pool || offset >= rt->const_pool_size) {
        printf("Error: Constant pool offset out of bounds in x86_64 JIT\n");
        goto jit_fail;
      }
      const uint64_t val =
          (uint64_t)(uintptr_t)(rt->const_pool + (size_t)offset);
      EMIT_PUSH_RAX_DS;
      EMITV(0x48, 0xB8); // mov rax, imm64
      EMIT_IMM64(val);
    } break;
    case ONDA_OP_PUSH_INSTRUCTION_ADDR: {
      uint32_t target_bpos = 0;
      READ_BCODE(uint32_t, target_bpos);
      if (target_bpos >= bytecode_size || bcode_to_mcode[target_bpos] < 0) {
        printf("Error: unresolved instruction address at bytecode %u\n",
               target_bpos);
        goto jit_fail;
      }
      EMIT_PUSH_RAX_DS;
      EMIT(0xB8); // mov eax, imm32 (machine-code byte offset from start)
      EMIT_IMM32((uint32_t)bcode_to_mcode[target_bpos]);
    } break;

    case ONDA_OP_PUSH_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT_PUSH_RAX_DS;
      EMIT_LOAD_LOCAL_TO_RAX(local_id);
    } break;

    case ONDA_OP_STORE_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT_STORE_LOCAL_FROM_RAX(local_id);
      EMIT_POP_DS_RAX;
    } break;

    case ONDA_OP_ADD:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x01, 0xC8); // add rax, rcx
      break;
    case ONDA_OP_ADD_CONST_I8:
      EMITV(0x48, 0x83, 0xC0, bytecode[bcode_pos++]); // add rax, imm8
      break;

    case ONDA_OP_SUB:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x29, 0xC1); // sub rcx, rax
      EMITV(0x48, 0x89, 0xC8); // mov rax, rcx
      break;

    case ONDA_OP_MUL:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x0F, 0xAF, 0xC1); // imul rax, rcx
      break;
    case ONDA_OP_MUL_CONST_I8:
      EMITV(0x48, 0x6B, 0xC0, bytecode[bcode_pos++]); // imul rax, rax, imm8
      break;

    case ONDA_OP_ADD_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT_LOCAL_TO_RCX(local_id);
      EMITV(0x48, 0x01, 0xC8); // add rax, rcx
    } break;

    case ONDA_OP_MUL_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      EMIT_LOCAL_TO_RCX(local_id);
      EMITV(0x48, 0x0F, 0xAF, 0xC1); // imul rax, rcx
    } break;

    case ONDA_OP_DIV:
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (save divisor)
      EMIT_POP_DS_RAX;         // rax = dividend
      EMITV(0x48, 0x99);       // cqo (sign-extend rax into rdx:rax)
      EMITV(0x48, 0xF7, 0xF9); // idiv rcx
      break;

    case ONDA_OP_MOD:
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (save divisor)
      EMIT_POP_DS_RAX;         // rax = dividend
      EMITV(0x48, 0x99);       // cqo (sign-extend rax into rdx:rax)
      EMITV(0x48, 0xF7, 0xF9); // idiv rcx
      EMITV(0x48, 0x89, 0xD0); // mov rax, rdx (remainder)
      break;
    case ONDA_OP_SHIFT_LEFT:
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (shift amount)
      EMIT_POP_DS_RAX;         // rax = value
      EMITV(0x48, 0xD3, 0xE0); // shl rax, cl
      break;
    case ONDA_OP_SHIFT_RIGHT:
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (shift amount)
      EMIT_POP_DS_RAX;         // rax = value
      EMITV(0x48, 0xD3, 0xF8); // sar rax, cl
      break;
    case ONDA_OP_BITWISE_AND:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x21, 0xC8); // and rax, rcx
      break;
    case ONDA_OP_BITWISE_OR:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x09, 0xC8); // or rax, rcx
      break;
    case ONDA_OP_BITWISE_XOR:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x31, 0xC8); // xor rax, rcx
      break;
    case ONDA_OP_BITWISE_NOT:
      EMITV(0x48, 0xF7, 0xD0); // not rax
      break;

    case ONDA_OP_AND:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x85, 0xC9); // test rcx, rcx
      EMITV(0x0F, 0x95, 0xC1); // setne cl
      EMITV(0x0F, 0xB6, 0xC9); // movzx ecx, cl
      EMITV(0x48, 0x85, 0xC0); // test rax, rax
      EMITV(0x0F, 0x95, 0xC0); // setne al
      EMITV(0x0F, 0xB6, 0xC0); // movzx eax, al
      EMITV(0x21, 0xC8);       // and eax, ecx
      break;

    case ONDA_OP_OR:
      EMIT_POP_DS_RCX;
      EMITV(0x48, 0x85, 0xC9); // test rcx, rcx
      EMITV(0x0F, 0x95, 0xC1); // setne cl
      EMITV(0x0F, 0xB6, 0xC9); // movzx ecx, cl
      EMITV(0x48, 0x85, 0xC0); // test rax, rax
      EMITV(0x0F, 0x95, 0xC0); // setne al
      EMITV(0x0F, 0xB6, 0xC0); // movzx eax, al
      EMITV(0x09, 0xC8);       // or eax, ecx
      break;

    case ONDA_OP_NOT:
      EMITV(0x48, 0x85, 0xC0); // test rax, rax
      EMITV(0x0F, 0x94, 0xC0); // sete al
      EMITV(0x0F, 0xB6, 0xC0); // movzx eax, al
      break;

    case ONDA_OP_INC:
      EMITV(0x48, 0xFF, 0xC0); // inc rax
      break;

    case ONDA_OP_DEC:
      EMITV(0x48, 0xFF, 0xC8); // dec rax
      break;
    case ONDA_OP_INC_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      if (IS_FAST_LOCAL(local_id)) {
        EMITV(0x49, 0xFF, k_inc_local_reg_modrm[LOCAL_REG_IDX(local_id)]);
      } else {
        const uint32_t off = (uint32_t)local_id * 8;
        if (off <= 127) {
          EMITV(0x49, 0xFF, 0x45, (uint8_t)off);
        } else {
          EMITV(0x49, 0xFF, 0x85);
          EMIT_IMM32(off);
        }
      }
    } break;
    case ONDA_OP_DEC_LOCAL: {
      const uint8_t local_id = bytecode[bcode_pos++];
      if (IS_FAST_LOCAL(local_id)) {
        EMITV(0x49, 0xFF, k_dec_local_reg_modrm[LOCAL_REG_IDX(local_id)]);
      } else {
        const uint32_t off = (uint32_t)local_id * 8;
        if (off <= 127) {
          EMITV(0x49, 0xFF, 0x4D, (uint8_t)off);
        } else {
          EMITV(0x49, 0xFF, 0x8D);
          EMIT_IMM32(off);
        }
      }
    } break;

    case ONDA_OP_EQ:
      EMIT_SETCC_FROM_POP_CMP(0x94); // sete al
      break;

    case ONDA_OP_NEQ:
      EMIT_SETCC_FROM_POP_CMP(0x95); // setne al
      break;

    case ONDA_OP_LT:
      EMIT_SETCC_FROM_POP_CMP(0x9C); // setl al
      break;

    case ONDA_OP_GT:
      EMIT_SETCC_FROM_POP_CMP(0x9F); // setg al
      break;

    case ONDA_OP_LTE:
      EMIT_SETCC_FROM_POP_CMP(0x9E); // setle al
      break;

    case ONDA_OP_GTE:
      EMIT_SETCC_FROM_POP_CMP(0x9D); // setge al
      break;

    case ONDA_OP_PUSH_FROM_ADDR_B:
      EMITV(0x0F, 0xB6, 0x00); // movzx eax, byte [rax]
      break;

    case ONDA_OP_STORE_TO_ADDR_B:
      EMIT_STORE_TO_ADDR(0, 0x88);
      break;

    case ONDA_OP_PUSH_FROM_ADDR_HW:
      EMITV(0x0F, 0xB7, 0x00); // movzx eax, word [rax]
      break;

    case ONDA_OP_STORE_TO_ADDR_HW:
      EMIT_STORE_TO_ADDR(0x66, 0x89);
      break;

    case ONDA_OP_PUSH_FROM_ADDR_W:
      EMITV(0x8B, 0x00); // mov eax, [rax] (zero-extends to rax)
      break;

    case ONDA_OP_STORE_TO_ADDR_W:
      EMIT_STORE_TO_ADDR(0, 0x89);
      break;

    case ONDA_OP_PUSH_FROM_ADDR_DW:
      EMITV(0x48, 0x8B, 0x00); // mov rax, [rax]
      break;

    case ONDA_OP_STORE_TO_ADDR_DW:
      EMIT_STORE_TO_ADDR(0x48, 0x89);
      break;

    case ONDA_OP_DUP:
      EMIT_PUSH_RAX_DS;
      break;

    case ONDA_OP_DROP:
      EMIT_POP_DS_RAX;
      break;

    case ONDA_OP_SWAP:
      EMIT_PEEK_DS_RCX;
      EMIT_STORE_RAX_DS;
      EMITV(0x48, 0x89, 0xC8); // mov rax, rcx
      break;

    case ONDA_OP_OVER:
      // ( a b -- a b a ): copy second to top
      EMIT_PEEK_DS_RCX;        // rcx = a
      EMIT_PUSH_RAX_DS;        // push b; [r12]=b [r12+8]=a
      EMITV(0x48, 0x89, 0xC8); // mov rax, rcx
      break;

    case ONDA_OP_ROT:
      // ( a b c -- c a b ): before: rax=c [r12]=b [r12+8]=a
      EMITV(0x49, 0x8B, 0x0C, 0x24);       // mov rcx, [r12]     =b
      EMITV(0x49, 0x8B, 0x54, 0x24, 0x08); // mov rdx, [r12+8]   =a
      EMITV(0x49, 0x89, 0x44, 0x24, 0x08); // mov [r12+8], rax   =c
      EMITV(0x49, 0x89, 0x14, 0x24);       // mov [r12], rdx     =a
      EMITV(0x48, 0x89, 0xC8);             // mov rax, rcx       =b
      break;

    case ONDA_OP_JUMP: {
      memcpy(&jmp_offset, &bytecode[bcode_pos], sizeof(jmp_offset));
      EMIT(0xE9); // jmp rel32
      EMIT_JUMP_REL32_OR_UNRESOLVED(ONDA_OP_JUMP, jmp_offset);
      bcode_pos += sizeof(jmp_offset);
    } break;

    case ONDA_OP_JUMP_IF_FALSE: {
      memcpy(&jmp_offset, &bytecode[bcode_pos], sizeof(jmp_offset));
      // Save condition, pop new TOS, then branch if condition was zero.
      // Use rcx so the subsequent flags-modifying instructions don't corrupt
      // the test before the je.
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (save condition)
      EMIT_POP_DS_RAX;         // pop new TOS
      EMITV(0x48, 0x85, 0xC9); // test rcx, rcx
      EMITV(0x0F, 0x84);       // je rel32
      EMIT_JUMP_REL32_OR_UNRESOLVED(ONDA_OP_JUMP_IF_FALSE, jmp_offset);
      bcode_pos += sizeof(jmp_offset);
    } break;
    case ONDA_OP_JUMP_TO_TOS: {
      // Bytecode semantic is absolute instruction address; in JIT we push
      // machine-code byte offsets and reconstruct the absolute pointer here.
      EMITV(0x48, 0x89, 0xC1); // mov rcx, rax (target offset)
      EMIT_POP_DS_RAX;         // pop new TOS
      {
        // lea rdx, [rip + disp32] -> function start (offset 0)
        const int32_t disp_to_start = -(int32_t)(mcode_size + 7);
        EMITV(0x48, 0x8D, 0x15);
        EMIT_IMM32((uint32_t)disp_to_start);
      }
      EMITV(0x48, 0x01, 0xD1); // add rcx, rdx
      EMITV(0xFF, 0xE1);       // jmp rcx
    } break;

    case ONDA_OP_CALL_NATIVE: {
      uint32_t idx;
      READ_BCODE(uint32_t, idx);
      uint64_t fn_addr = (uint64_t)(uintptr_t)reg->items[idx].fn;
      EMIT_SAVE_LOCAL_REGS;
      EMIT_PUSH_RAX_DS;        // push TOS to data stack
      EMITV(0x4C, 0x89, 0xE7); // mov rdi, r12  (arg: DS ptr)
      EMITV(0x49, 0xBA);       // mov r10, imm64
      EMIT_IMM64(fn_addr);
      EMITV(0x41, 0xFF, 0xD2); // call r10
      EMITV(0x49, 0x89, 0xC4); // mov r12, rax  (update DS)
      EMIT_POP_DS_RAX;         // pop new TOS
      EMIT_RESTORE_LOCAL_REGS;
    } break;

    case ONDA_OP_CALL: {
      const uint8_t argc = bytecode[bcode_pos++];
      const uint8_t locals = bytecode[bcode_pos++];
      int32_t branch_offset;
      READ_BCODE(int32_t, branch_offset);

      ENSURE_CAPACITY(64u + (size_t)argc * 16u);

      const uint32_t frame_slots = 2u + (locals > ONDA_LOCAL_REG_COUNT
                                             ? (uint32_t)locals
                                             : (uint32_t)ONDA_LOCAL_REG_COUNT);
      const uint32_t frame_bytes = frame_slots * 8u;

      // mov rcx, r13  -- save prev frame_bp
      EMITV(0x4C, 0x89, 0xE9);

      // sub r13, frame_bytes
      EMIT_ADD_REG_IMM32(0xED, 0xED, frame_bytes);
      EMIT_SAVE_LOCAL_REGS;

      // Copy args from data stack to frame slots frame[2..2+argc-1]
      // VM layout: frame_bp[2+i] = sp[argc-i-1]
      //   i=0: deepest = sp[argc-1]; i=argc-1: TOS = sp[0] = rax
      for (int i = 0; i < (int)argc; i++) {
        const int ds_j = argc - i - 1; // index into data stack (0=TOS=rax)
        const uint8_t local_reg_idx = (uint8_t)i;
        if (ds_j == 0) {
          if (local_reg_idx < ONDA_LOCAL_REG_COUNT) {
            EMIT_MOV_LOCAL_FROM_RAX(local_reg_idx);
          } else {
            const uint32_t frame_off = (uint32_t)(2 + i) * 8;
            EMIT_STORE_FRAME_RAX(frame_off);
          }
        } else {
          // [r12 + (ds_j-1)*8]
          const uint32_t ds_off = (uint32_t)(ds_j - 1) * 8;
          EMIT_LOAD_DS_RDX(ds_off);
          if (local_reg_idx < ONDA_LOCAL_REG_COUNT) {
            EMIT_MOV_LOCAL_FROM_RDX(local_reg_idx);
          } else {
            const uint32_t frame_off = (uint32_t)(2 + i) * 8;
            EMIT_STORE_FRAME_RDX(frame_off);
          }
        }
      }

      // Pop args from data stack
      if (argc > 0) {
        const uint32_t new_tos_off = (uint32_t)(argc - 1u) * 8u;
        const uint32_t pop_bytes = (uint32_t)argc * 8u;
        // mov rax, [r12 + new_tos_off]  -- new TOS
        EMIT_LOAD_DS_RAX(new_tos_off);
        // add r12, pop_bytes
        EMIT_ADD_REG_IMM32(0xC4, 0xC4, pop_bytes);
      }

      // mov [r13+8], rcx  -- frame[1] = prev frame_bp
      EMITV(0x49, 0x89, 0x4D, 0x08);

      // lea rdi, [rip+9]  -- return addr = (end of LEA)+9 = after jmp below
      // After LEA: mov [r13+0],rdi (4B) + jmp rel32 (5B) = 9B
      EMITV(0x48, 0x8D, 0x3D);
      EMIT_IMM32(9);

      // mov [r13+0], rdi  -- frame[0] = return address
      EMITV(0x49, 0x89, 0x7D, 0x00);

      // jmp to callee (5 bytes)
      const size_t target_bpos = (size_t)((ssize_t)bcode_pos + branch_offset);
      EMIT(0xE9);
      if (bcode_to_mcode[target_bpos] == -1) {
        printf("Error: Unresolved CALL target at bcode_pos %zu\n", bcode_pos);
        goto jit_fail;
      }
      {
        const int32_t rel32 = (int32_t)((int64_t)bcode_to_mcode[target_bpos] -
                                        (int64_t)(mcode_size + 4));
        EMIT_IMM32((uint32_t)rel32);
      }
      // return point lands here (rdi pointed here via LEA)
    } break;

    case ONDA_OP_RET:
      EMITV(0x49, 0x8B, 0x4D, 0x08); // mov rcx, [r13+8]
      EMITV(0x48, 0x85, 0xC9);       // test rcx, rcx
      EMITV(0x75, 0x06);             // jne +6 (skip root ret)
      // root return (frame[1]==0 → called from C):
      EMITV(0x41, 0x5C); // pop r12
      EMITV(0x41, 0x5D); // pop r13
      EMIT(0x5D);        // pop rbp
      EMIT(0xC3);        // ret
      // nested return (frame[1] = prev frame_bp):
      EMIT_RESTORE_LOCAL_REGS;
      EMITV(0x49, 0x8B, 0x7D, 0x00); // mov rdi, [r13+0]
      EMITV(0x49, 0x89, 0xCD);       // mov r13, rcx
      EMITV(0xFF, 0xE7);             // jmp rdi
      break;

    default:
      printf("Error: Unimplemented opcode %02X in x86_64 JIT\n", opcode);
      goto jit_fail;
    }
  }

  if (unresolved_jumps) {
    printf("Error: Unresolved jumps remain after x86_64 JIT compilation.\n");
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

  onda_free(bcode_to_mcode);
  *out_machine_code = mcode;
  *out_machine_code_size = mcode_size;
  return 0;
jit_fail: {
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
}

#endif // defined(__x86_64__) && ONDA_ENABLE_JIT_X86_64
