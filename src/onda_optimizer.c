#include "onda_optimizer.h"

#include "onda_vm.h"

#include <string.h>

static inline bool opt_push_opcode(onda_code_obj_t* cobj, uint8_t opcode) {
  return onda_code_obj_emit_opcode(cobj, opcode) == 0;
}

static inline bool opt_push_u8(onda_code_obj_t* cobj, uint8_t value) {
  return onda_code_obj_emit_u8(cobj, value) == 0;
}

static bool opt_push_const_i64(onda_code_obj_t* cobj, int64_t val) {
  if (val >= 0 && val <= 0x7F) {
    if (!opt_push_opcode(cobj, ONDA_OP_PUSH_CONST_U8))
      return false;
    return opt_push_u8(cobj, (uint8_t)val);
  }

  if (val >= 0 && val <= INT32_MAX) {
    const uint32_t u32 = (uint32_t)val;
    if (!opt_push_opcode(cobj, ONDA_OP_PUSH_CONST_U32) ||
        !onda_code_obj_reserve(cobj, sizeof(uint32_t))) {
      return false;
    }
    memcpy(&cobj->code[cobj->size], &u32, sizeof(uint32_t));
    cobj->size += sizeof(uint32_t);
    return true;
  }

  const uint64_t u64 = (uint64_t)val;
  if (!opt_push_opcode(cobj, ONDA_OP_PUSH_CONST_U64) ||
      !onda_code_obj_reserve(cobj, sizeof(uint64_t))) {
    return false;
  }
  memcpy(&cobj->code[cobj->size], &u64, sizeof(uint64_t));
  cobj->size += sizeof(uint64_t);
  return true;
}

static bool fold_imm_arith(onda_code_obj_t* cobj) {
  if (cobj->recent_opcode_count < 2)
    return false;

  const uint8_t op = cobj->recent_opcodes[cobj->recent_opcode_count - 1];
  if (op != ONDA_OP_ADD && op != ONDA_OP_MUL)
    return false;

  if (cobj->recent_opcodes[cobj->recent_opcode_count - 2] !=
      ONDA_OP_PUSH_CONST_U8) {
    return false;
  }

  const size_t push_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 2];
  const uint8_t imm8 = cobj->code[push_pos + 1];

  cobj->size = push_pos;
  onda_code_obj_recent_trim(cobj, cobj->size);
  if (!opt_push_opcode(cobj,
                       op == ONDA_OP_ADD ? ONDA_OP_ADD_CONST_I8
                                         : ONDA_OP_MUL_CONST_I8) ||
      !opt_push_u8(cobj, imm8)) {
    return false;
  }
  return true;
}

static bool fold_const_imm_arith(onda_code_obj_t* cobj) {
  if (cobj->recent_opcode_count < 2)
    return false;

  const uint8_t op = cobj->recent_opcodes[cobj->recent_opcode_count - 1];
  if (op != ONDA_OP_ADD_CONST_I8 && op != ONDA_OP_MUL_CONST_I8)
    return false;

  const uint8_t prev = cobj->recent_opcodes[cobj->recent_opcode_count - 2];
  if (prev != ONDA_OP_PUSH_CONST_U8 && prev != ONDA_OP_PUSH_CONST_U32 &&
      prev != ONDA_OP_PUSH_CONST_U64) {
    return false;
  }

  const size_t push_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 2];
  const size_t op_pos = cobj->recent_opcode_pos[cobj->recent_opcode_count - 1];
  const int8_t imm8 = (int8_t)cobj->code[op_pos + 1];

  int64_t lhs = 0;
  if (prev == ONDA_OP_PUSH_CONST_U8) {
    lhs = (int8_t)cobj->code[push_pos + 1];
  } else if (prev == ONDA_OP_PUSH_CONST_U32) {
    uint32_t u32 = 0;
    memcpy(&u32, &cobj->code[push_pos + 1], sizeof(uint32_t));
    lhs = (int32_t)u32;
  } else {
    uint64_t u64 = 0;
    memcpy(&u64, &cobj->code[push_pos + 1], sizeof(uint64_t));
    lhs = (int64_t)u64;
  }

  const int64_t res =
      (op == ONDA_OP_ADD_CONST_I8) ? (lhs + (int64_t)imm8)
                                   : (lhs * (int64_t)imm8);

  cobj->size = push_pos;
  onda_code_obj_recent_trim(cobj, cobj->size);
  return opt_push_const_i64(cobj, res);
}

static bool fold_eq_zero_to_not(onda_code_obj_t* cobj) {
  if (cobj->recent_opcode_count < 2)
    return false;

  if (cobj->recent_opcodes[cobj->recent_opcode_count - 1] != ONDA_OP_EQ)
    return false;

  const uint8_t prev = cobj->recent_opcodes[cobj->recent_opcode_count - 2];
  const size_t push_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 2];

  bool is_zero = false;
  if (prev == ONDA_OP_PUSH_CONST_U8) {
    is_zero = cobj->code[push_pos + 1] == 0;
  } else if (prev == ONDA_OP_PUSH_CONST_U32) {
    uint32_t u32 = 1;
    memcpy(&u32, &cobj->code[push_pos + 1], sizeof(uint32_t));
    is_zero = u32 == 0;
  } else if (prev == ONDA_OP_PUSH_CONST_U64) {
    uint64_t u64 = 1;
    memcpy(&u64, &cobj->code[push_pos + 1], sizeof(uint64_t));
    is_zero = u64 == 0;
  }

  if (!is_zero)
    return false;

  cobj->size = push_pos;
  onda_code_obj_recent_trim(cobj, cobj->size);
  return opt_push_opcode(cobj, ONDA_OP_NOT);
}

static bool fold_local_arith(onda_code_obj_t* cobj) {
  if (cobj->recent_opcode_count < 2)
    return false;

  const uint8_t op = cobj->recent_opcodes[cobj->recent_opcode_count - 1];
  if (op != ONDA_OP_ADD && op != ONDA_OP_MUL)
    return false;

  if (cobj->recent_opcodes[cobj->recent_opcode_count - 2] !=
      ONDA_OP_PUSH_LOCAL) {
    return false;
  }

  const size_t push_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 2];
  const uint8_t local_id = cobj->code[push_pos + 1];

  cobj->size = push_pos;
  onda_code_obj_recent_trim(cobj, cobj->size);
  if (!opt_push_opcode(cobj,
                       op == ONDA_OP_ADD ? ONDA_OP_ADD_LOCAL
                                         : ONDA_OP_MUL_LOCAL) ||
      !opt_push_u8(cobj, local_id)) {
    return false;
  }
  return true;
}

static bool fold_inc_dec_local(onda_code_obj_t* cobj) {
  if (cobj->recent_opcode_count < 3)
    return false;

  if (cobj->recent_opcodes[cobj->recent_opcode_count - 3] !=
          ONDA_OP_PUSH_LOCAL ||
      cobj->recent_opcodes[cobj->recent_opcode_count - 1] !=
          ONDA_OP_STORE_LOCAL) {
    return false;
  }

  const uint8_t mid = cobj->recent_opcodes[cobj->recent_opcode_count - 2];
  if (mid != ONDA_OP_INC && mid != ONDA_OP_DEC)
    return false;

  const size_t push_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 3];
  const size_t store_pos =
      cobj->recent_opcode_pos[cobj->recent_opcode_count - 1];
  const uint8_t push_local = cobj->code[push_pos + 1];
  const uint8_t store_local = cobj->code[store_pos + 1];

  if (push_local != store_local)
    return false;

  cobj->size = push_pos;
  onda_code_obj_recent_trim(cobj, cobj->size);
  if (!opt_push_opcode(cobj,
                       mid == ONDA_OP_INC ? ONDA_OP_INC_LOCAL
                                          : ONDA_OP_DEC_LOCAL) ||
      !opt_push_u8(cobj, push_local)) {
    return false;
  }
  return true;
}

typedef bool (*opt_rule_fn)(onda_code_obj_t* cobj);

static bool fold_add_mul_ops(onda_code_obj_t* cobj) {
  if (fold_imm_arith(cobj))
    return true;
  return fold_local_arith(cobj);
}

static bool run_last_opcode_rule(onda_code_obj_t* cobj) {
  static const opt_rule_fn by_last_opcode[ONDA_OP_COUNT] = {
      [ONDA_OP_ADD] = fold_add_mul_ops,
      [ONDA_OP_MUL] = fold_add_mul_ops,
      [ONDA_OP_ADD_CONST_I8] = fold_const_imm_arith,
      [ONDA_OP_MUL_CONST_I8] = fold_const_imm_arith,
      [ONDA_OP_EQ] = fold_eq_zero_to_not,
      [ONDA_OP_STORE_LOCAL] = fold_inc_dec_local,
  };

  if (cobj->recent_opcode_count == 0)
    return false;

  const uint8_t last = cobj->recent_opcodes[cobj->recent_opcode_count - 1];
  const opt_rule_fn rule = by_last_opcode[last];
  return rule ? rule(cobj) : false;
}

bool onda_try_optimize(onda_code_obj_t* cobj) {
  bool changed = false;
  while (run_last_opcode_rule(cobj)) {
    changed = true;
  }
  return changed;
}
