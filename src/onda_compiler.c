#include "onda_compiler.h"
#include "onda_optimizer.h"

#include "onda_dict.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline char curr(onda_lexer_t* lx) {
  return lx->src[lx->pos];
}
static inline char nextc(onda_lexer_t* lx) {
  return lx->src[lx->pos + 1];
}
static inline bool at_end(onda_lexer_t* lx) {
  return lx->src[lx->pos] == '\0';
}

static inline void advance(onda_lexer_t* lx) {
  if (!at_end(lx)) {
    if (lx->src[lx->pos] == '\n') {
      lx->line++;
      lx->column = 0;
    } else {
      lx->column++;
    }
    lx->pos++;
  }
}

static void skip_whitespace_and_comments(onda_lexer_t* lx) {
  for (;;) {
    char c = curr(lx);
    if (c == '#') {
      while (!at_end(lx) && curr(lx) != '\n')
        advance(lx);
    } else if (isspace((unsigned char)c)) {
      advance(lx);
    } else {
      break;
    }
  }
}

// Decode a string literal (without surrounding quotes) into arena memory.
// Supports escapes: \n \t \r \" \\ . Returns 0 on success.
static int lex_string(onda_lexer_t* lx, const char** dst, size_t* dst_len) {
  // opening '"' already at curr(lx)
  advance(lx); // skip the opening quote
  size_t start_pos = lx->pos;

  // First pass: find end, track length ignoring escapes to size buffer loosely
  while (!at_end(lx) && curr(lx) != '"') {
    if (curr(lx) == '\\' && nextc(lx) != '\0') {
      // skip the escape and next char
      advance(lx);
      advance(lx);
      continue;
    }
    advance(lx);
  }

  if (at_end(lx) || curr(lx) != '"')
    return -1; // unterminated

  size_t end_pos = lx->pos; // position of closing quote
  // Allocate a buffer of worst-case size (every escape becomes 1 char)
  int raw_len = (int)(end_pos - start_pos);
  char* buf = (char*)onda_calloc(1, (size_t)raw_len + 1);
  if (!buf)
    return -1;

  // Second pass: actually decode
  const char* src = lx->src + start_pos;
  int si = 0;
  int di = 0;
  while (si < raw_len) {
    char c = src[si++];
    if (c == '\\' && si < raw_len) {
      char esc = src[si++];
      switch (esc) {
      case 'n':
        buf[di++] = '\n';
        break;
      case 't':
        buf[di++] = '\t';
        break;
      case 'r':
        buf[di++] = '\r';
        break;
      case '"':
        buf[di++] = '"';
        break;
      case '\\':
        buf[di++] = '\\';
        break;
      default:
        buf[di++] = esc;
        break; // unknown escape -> literal
      }
    } else {
      buf[di++] = c;
    }
  }
  buf[di] = '\0';

  // consume closing quote
  advance(lx);

  *dst = buf;
  *dst_len = di;
  return 0;
}

static inline void print_err(onda_lexer_t* lx, const char* msg, ...) {
  fprintf(stderr,
          "Error at %s:%zu:%zu ",
          lx->filename ? lx->filename : "<input>",
          lx->line,
          lx->column);
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  va_end(args);
}

static int parse_number(const char* s, size_t len, int64_t* out) {
  static char tmp[32]; // enough for 64-bit integers
  if (len >= sizeof(tmp)) {
    fprintf(stderr, "Error: number literal too long\n");
    return -1;
  }
  memcpy(tmp, s, len);
  tmp[len] = '\0';
  const size_t sign_off = (tmp[0] == '-') ? 1 : 0;
  if (tmp[sign_off] == '0' &&
      (tmp[sign_off + 1] == 'b' || tmp[sign_off + 1] == 'B')) {
    int64_t val = 0;
    for (size_t i = sign_off + 2; i < len; i++) {
      val = (val << 1) + (tmp[i] - '0');
    }
    *out = (sign_off == 0) ? val : -val;
    return 0;
  }
  char* endptr = NULL;
  *out = strtoll(tmp, &endptr, 0);
  return 0;
}

static int lex_number(onda_lexer_t* lx) {
  size_t start = lx->pos;

  if (curr(lx) == '-' && isdigit((unsigned char)nextc(lx)))
    advance(lx);

  // Parse hex number
  if (curr(lx) == '0' && (nextc(lx) == 'x' || nextc(lx) == 'X') &&
      isxdigit((unsigned char)lx->src[lx->pos + 2])) {
    advance(lx); // 0
    advance(lx); // x
    while (isxdigit((unsigned char)curr(lx)))
      advance(lx);
    return (int)(lx->pos - start);
  }

  // Parse binary number
  if (curr(lx) == '0' && (nextc(lx) == 'b' || nextc(lx) == 'B') &&
      (lx->src[lx->pos + 2] == '0' || lx->src[lx->pos + 2] == '1')) {
    advance(lx); // 0
    advance(lx); // b
    while (curr(lx) == '0' || curr(lx) == '1')
      advance(lx);
    return (int)(lx->pos - start);
  }

  // Parse decimal number (optionally with fractional part, but we only support
  // integers for now)
  while (isdigit((unsigned char)curr(lx)))
    advance(lx);

  if (curr(lx) == '.' && isdigit((unsigned char)nextc(lx))) {
    advance(lx); // dot
    while (isdigit((unsigned char)curr(lx)))
      advance(lx);
  }

  return (int)(lx->pos - start);
}

static inline void tok1(onda_lexer_t* l, onda_token_t* t, int type) {
  t->type = type;
  t->len = 1;
  advance(l);
}

static inline bool tok_is_ident(const onda_token_t* tok,
                                const char* ident,
                                size_t ident_len) {
  return tok->type == TOKEN_IDENTIFIER && tok->len == ident_len &&
         strncmp(tok->start, ident, ident_len) == 0;
}

void onda_token_next(onda_lexer_t* lexer, onda_token_t* t) {
  if (at_end(lexer)) {
    t->type = TOKEN_EOF;
    t->start = lexer->src + lexer->pos;
    t->len = 0;
    return;
  }

  skip_whitespace_and_comments(lexer);

  t->start = lexer->src + lexer->pos;
  t->len = 1;

  const unsigned char c = (unsigned char)curr(lexer);
  switch (c) {
  case ':':
    return tok1(lexer, t, TOKEN_COLON);
  case ';':
    return tok1(lexer, t, TOKEN_SEMICOLON);
  case '(':
    return tok1(lexer, t, TOKEN_LPAREN);
  case ')':
    return tok1(lexer, t, TOKEN_RPAREN);
  case '"': {
    int rc = lex_string(lexer, &t->start, &t->len);
    if (rc) {
      t->type = TOKEN_INVALID;
      t->start = lexer->src + lexer->pos;
      t->len = 0;
    } else
      t->type = TOKEN_STRING;
    return;
  }
  }

  if (isdigit(c) || (c == '-' && isdigit((unsigned char)nextc(lexer)))) {
    t->type = TOKEN_NUMBER;
    t->len = lex_number(lexer);
    if (parse_number(t->start, t->len, &t->number) != 0) {
      t->type = TOKEN_INVALID;
      t->len = 0;
    }
    return;
  }

  t->type = TOKEN_IDENTIFIER;
  size_t start_pos = lexer->pos;
  while (!at_end(lexer) && !isspace((unsigned char)curr(lexer)))
    advance(lexer);
  t->len = (int)(lexer->pos - start_pos);
  if (t->len == 0) {
    t->type = TOKEN_EOF;
    t->start = lexer->src + lexer->pos;
    return;
  }
}

static void onda_token_peek(onda_lexer_t* lexer, onda_token_t* t) {
  size_t saved_pos = lexer->pos, saved_line = lexer->line,
         saved_column = lexer->column;
  onda_token_next(lexer, t);
  lexer->pos = saved_pos;
  lexer->line = saved_line;
  lexer->column = saved_column;
}

int onda_code_obj_reserve(onda_code_obj_t* cobj, size_t extra) {
  if (cobj->size + extra <= cobj->capacity)
    return 0;

  size_t new_capacity = cobj->capacity ? cobj->capacity * 2 : 256;
  while (new_capacity < cobj->size + extra)
    new_capacity *= 2;

  uint8_t* new_code = onda_realloc(cobj->code, new_capacity);
  if (!new_code)
    return -1;

  cobj->code = new_code;
  cobj->capacity = new_capacity;
  return 0;
}

int onda_code_obj_emit_u8(onda_code_obj_t* cobj, uint8_t value) {
  if (onda_code_obj_reserve(cobj, 1) != 0)
    return -1;
  cobj->code[cobj->size++] = value;
  return 0;
}

int onda_code_obj_emit_bytes(onda_code_obj_t* cobj,
                             const void* src,
                             size_t len) {
  if (onda_code_obj_reserve(cobj, len) != 0)
    return -1;
  memcpy(&cobj->code[cobj->size], src, len);
  cobj->size += len;
  return 0;
}

void onda_code_obj_recent_push(onda_code_obj_t* cobj,
                               uint8_t opcode,
                               size_t opcode_pos) {
  if (cobj->recent_opcode_count < 3) {
    const uint8_t idx = cobj->recent_opcode_count++;
    cobj->recent_opcodes[idx] = opcode;
    cobj->recent_opcode_pos[idx] = opcode_pos;
    return;
  }
  cobj->recent_opcodes[0] = cobj->recent_opcodes[1];
  cobj->recent_opcodes[1] = cobj->recent_opcodes[2];
  cobj->recent_opcodes[2] = opcode;
  cobj->recent_opcode_pos[0] = cobj->recent_opcode_pos[1];
  cobj->recent_opcode_pos[1] = cobj->recent_opcode_pos[2];
  cobj->recent_opcode_pos[2] = opcode_pos;
}

void onda_code_obj_recent_trim(onda_code_obj_t* cobj, size_t new_size) {
  while (cobj->recent_opcode_count > 0 &&
         cobj->recent_opcode_pos[cobj->recent_opcode_count - 1] >= new_size) {
    cobj->recent_opcode_count--;
  }
}

int onda_code_obj_emit_opcode(onda_code_obj_t* cobj, uint8_t opcode) {
  const size_t opcode_pos = cobj->size;
  if (onda_code_obj_emit_u8(cobj, opcode) != 0)
    return -1;
  onda_code_obj_recent_push(cobj, opcode, opcode_pos);
  return 0;
}

#define CODE_PUSH_BYTE(val)                                                    \
  do {                                                                         \
    if (onda_code_obj_emit_u8(cobj, (uint8_t)(val)) != 0) {                   \
      fprintf(stderr, "Code buffer overflow\n");                               \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CODE_PUSH_BYTES(src, len)                                              \
  do {                                                                         \
    if (onda_code_obj_emit_bytes(cobj, (src), (len)) != 0) {                  \
      fprintf(stderr, "Code buffer overflow\n");                               \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CODE_PUSH_OPCODE(op)                                                   \
  do {                                                                         \
    if (onda_code_obj_emit_opcode(cobj, (op)) != 0) {                         \
      fprintf(stderr, "Code buffer overflow\n");                               \
      return -1;                                                               \
    }                                                                          \
  } while (0)

static inline int code_pool_push_string(onda_code_obj_t* cobj,
                                        const char* str,
                                        size_t str_len,
                                        uint32_t* out_offset) {
  const size_t size_needed = str_len + 1;
  if (cobj->const_pool_size + size_needed > UINT32_MAX) {
    fprintf(stderr, "Constant pool overflow\n");
    return -1;
  }
  if (cobj->const_pool_size + size_needed > cobj->const_pool_capacity) {
    size_t new_capacity =
        cobj->const_pool_capacity ? cobj->const_pool_capacity * 2 : 256;
    while (new_capacity < cobj->const_pool_size + size_needed)
      new_capacity *= 2;
    uint8_t* new_pool = onda_realloc(cobj->const_pool, new_capacity);
    if (!new_pool) {
      fprintf(stderr, "Failed to allocate constant pool\n");
      return -1;
    }
    cobj->const_pool = new_pool;
    cobj->const_pool_capacity = new_capacity;
  }
  *out_offset = (uint32_t)cobj->const_pool_size;
  memcpy(cobj->const_pool + cobj->const_pool_size, str, str_len);
  cobj->const_pool[cobj->const_pool_size + str_len] = '\0';
  cobj->const_pool_size += size_needed;
  return 0;
}

// Create new scope and push onto code object's scope stack
static inline void onda_scope_push(onda_code_obj_t* code) {
  onda_scope_t* new_scope = (onda_scope_t*)onda_malloc(sizeof(onda_scope_t));
  onda_dict_init(&new_scope->locals);
  new_scope->locals_count = 0;
  new_scope->parent = code->current_scope;
  code->current_scope = new_scope;
}

// Look up a variable name in the current scope stack.
// If found, set local_id and return 0.
static inline int onda_scope_get(onda_code_obj_t* code,
                                 const char* name,
                                 size_t name_len,
                                 uint8_t* local_id) {
  onda_scope_t* scope = code->current_scope;
  uint64_t out_var_id;
  while (scope) {
    if (onda_dict_get(&scope->locals, name, name_len, &out_var_id) == 0) {
      *local_id = (uint8_t)out_var_id;
      return 0; // found
    }
    scope = scope->parent;
  }
  return -1; // not found
}

// Define a new variable in the current scope with the given name and local_id.
static inline int onda_scope_set(onda_code_obj_t* code,
                                 const char* name,
                                 size_t name_len,
                                 uint8_t local_id) {

  onda_scope_t* scope = code->current_scope;
  uint64_t existing_id;
  if (onda_dict_get(&scope->locals, name, name_len, &existing_id) == 0) {
    printf("Error: Variable '%.*s' already defined in this scope\n",
           (int)name_len,
           name);
    return -1;
  }
  onda_dict_put(&scope->locals, name, name_len, local_id);
  return 0;
}

// Pop the current scope off the stack, freeing its resources. Update parent
// scope's peak locals count if needed.
static inline void onda_scope_pop(onda_code_obj_t* code) {
  onda_scope_t* scope = code->current_scope;
  if (scope == NULL) {
    printf("Error: No scope to pop\n");
    return;
  }
  code->current_scope = scope->parent;
  onda_dict_free(&scope->locals);
  free(scope);
}

static int onda_compile_expr(onda_lexer_t* lexer,
                             onda_env_t* env,
                             onda_code_obj_t* cobj);

static inline int onda_compile_until_ident(onda_lexer_t* lexer,
                                           onda_env_t* env,
                                           onda_code_obj_t* cobj,
                                           const char* ident_a,
                                           const size_t ident_a_len,
                                           const char* ident_b,
                                           const size_t ident_b_len) {
  onda_token_t tok;
  while (true) {
    onda_token_peek(lexer, &tok);
    if (tok_is_ident(&tok, ident_a, ident_a_len)) {
      onda_token_next(lexer, &tok); // consume ident_a
      return 0;
    }
    if (ident_b && tok_is_ident(&tok, ident_b, ident_b_len)) {
      onda_token_next(lexer, &tok); // consume ident_b
      return 1;
    }
    int rc = onda_compile_expr(lexer, env, cobj);
    if (rc != 0)
      return rc;
  }
  return -1; // should never reach
}

static inline void code_patch_rel_i16(onda_code_obj_t* cobj,
                                      size_t from_pc,
                                      size_t to_pc) {
  const int16_t offset = (int16_t)(to_pc - from_pc);
  memcpy(&cobj->code[from_pc], &offset, sizeof(offset));
}

static int onda_compile_if(onda_lexer_t* lexer,
                           onda_env_t* env,
                           onda_code_obj_t* cobj) {
  int rc;
  rc = onda_compile_until_ident(lexer, env, cobj, "then", 4, NULL, 0);
  if (rc < 0)
    return rc;

  CODE_PUSH_OPCODE(ONDA_OP_JUMP_IF_FALSE);
  size_t condition_jmp_pc = cobj->size;
  CODE_PUSH_BYTES(&(int16_t){0},
                  sizeof(int16_t)); // placeholder for jump offset
  rc = onda_compile_until_ident(lexer, env, cobj, "else", 4, "end", 3);
  if (rc < 0)
    return rc;

  // If we ended on "else", we need to emit a jump over the else block
  size_t then_jmp_pc;
  if (rc == 0) {
    CODE_PUSH_OPCODE(ONDA_OP_JUMP);
    then_jmp_pc = cobj->size;
    CODE_PUSH_BYTES(&(int16_t){0},
                    sizeof(int16_t)); // placeholder for jump offset
  }

  // Patch the jump offset for the condition
  code_patch_rel_i16(cobj, condition_jmp_pc, cobj->size);

  // If we ended on "else", we need to compile the else block too
  if (rc == 0) {
    rc = onda_compile_until_ident(lexer, env, cobj, "end", 3, NULL, 0);
    if (rc < 0)
      return rc;

    // Patch the jump offset for the then block
    code_patch_rel_i16(cobj, then_jmp_pc, cobj->size);
  }

  return 0;
}

static int onda_compile_while(onda_lexer_t* lexer,
                              onda_env_t* env,
                              onda_code_obj_t* cobj) {
  const int32_t prev_loop_start_pc = cobj->inner_loop_start_pc;
  size_t loop_start_pc = cobj->size;
  cobj->inner_loop_start_pc = (int32_t)loop_start_pc;
  int rc = onda_compile_until_ident(lexer, env, cobj, "do", 2, NULL, 0);
  if (rc < 0)
    return rc;

  CODE_PUSH_OPCODE(ONDA_OP_JUMP_IF_FALSE);
  size_t condition_jmp_pc = cobj->size;
  CODE_PUSH_BYTES(&(int16_t){0},
                  sizeof(int16_t)); // placeholder for jump offset

  rc = onda_compile_until_ident(lexer, env, cobj, "end", 3, NULL, 0);
  if (rc < 0)
    return rc;

  // Emit jump back to loop start
  CODE_PUSH_OPCODE(ONDA_OP_JUMP);
  size_t loop_end_jmp_pc = cobj->size;
  const int16_t loop_back_offset = (int16_t)(loop_start_pc - loop_end_jmp_pc);
  CODE_PUSH_BYTES(&loop_back_offset, sizeof(int16_t));

  // Patch the jump offset for the condition
  code_patch_rel_i16(cobj, condition_jmp_pc, cobj->size);

  // Restore previous loop start for nested loops
  cobj->inner_loop_start_pc = prev_loop_start_pc;

  return 0;
}

static char* read_file(const char* filepath, size_t* out_size) {
  FILE* f = fopen(filepath, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (file_size < 0) {
    fclose(f);
    return NULL;
  }
  char* buffer = (char*)onda_malloc((size_t)file_size + 1);
  if (!buffer) {
    fclose(f);
    return NULL;
  }
  size_t read_size = fread(buffer, 1, (size_t)file_size, f);
  fclose(f);
  if (read_size != (size_t)file_size) {
    onda_free(buffer);
    return NULL;
  }
  buffer[read_size] = '\0'; // null-terminate for lexer
  *out_size = read_size;
  return buffer;
}

static int onda_compile_store_local(onda_lexer_t* lexer,
                                    onda_env_t* env,
                                    onda_code_obj_t* cobj) {
  (void)env; // unused arg
  onda_token_t tok;
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_IDENTIFIER) {
    print_err(lexer, "Expected local variable name after '->'");
    return -1;
  }
  uint8_t local_id;
  if (onda_scope_get(cobj, tok.start, tok.len, &local_id) != 0) {
    print_err(lexer, "Undefined variable in store operation");
    return -1;
  }
  const uint8_t local_slot = local_id + ONDA_LOCALS_BASE_OFF;
  CODE_PUSH_OPCODE(ONDA_OP_STORE_LOCAL);
  CODE_PUSH_BYTE(local_slot);
  (void)onda_try_optimize(cobj);
  return 0;
}

static int onda_compile_import(onda_lexer_t* lexer,
                               onda_env_t* env,
                               onda_code_obj_t* cobj) {
  onda_token_t tok;
  char path[128];
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_STRING) {
    print_err(lexer, "Expected string literal after 'import'");
    return -1;
  }

  if (tok.len >= sizeof(path)) {
    print_err(lexer, "Import path too long");
    return -1;
  }

  memcpy(path, tok.start, (size_t)tok.len);
  path[tok.len] = '\0';

  return onda_compile_file(path, lexer, env, cobj);
}

static int onda_compile_continue(onda_lexer_t* lexer,
                                 onda_env_t* env,
                                 onda_code_obj_t* cobj) {
  (void)env; // unused arg
  if (cobj->inner_loop_start_pc == -1) {
    print_err(lexer, "'continue' word used outside of a loop");
    return -1;
  }
  // Emit jump back to loop start
  CODE_PUSH_OPCODE(ONDA_OP_JUMP);
  size_t loop_end_jmp_pc = cobj->size;
  const int16_t loop_back_offset =
      (int16_t)(cobj->inner_loop_start_pc - loop_end_jmp_pc);
  CODE_PUSH_BYTES(&loop_back_offset, sizeof(int16_t));
  return 0;
}

typedef int (*imm_word_handler_t)(onda_lexer_t* lexer,
                                  onda_env_t* env,
                                  onda_code_obj_t* cobj);

typedef struct {
  const char* name;
  uint8_t opcode;
  imm_word_handler_t handler;
} onda_imm_word_t;

static const onda_imm_word_t imm_words[] = {
    // Operators
    {"+", ONDA_OP_ADD, .handler = NULL},
    {"-", ONDA_OP_SUB, .handler = NULL},
    {"*", ONDA_OP_MUL, .handler = NULL},
    {"/", ONDA_OP_DIV, .handler = NULL},
    {"%", ONDA_OP_MOD, .handler = NULL},
    {"++", ONDA_OP_INC, .handler = NULL},
    {"--", ONDA_OP_DEC, .handler = NULL},
    {"<<", ONDA_OP_SHIFT_LEFT, .handler = NULL},
    {">>", ONDA_OP_SHIFT_RIGHT, .handler = NULL},
    {"&", ONDA_OP_BITWISE_AND, .handler = NULL},
    {"|", ONDA_OP_BITWISE_OR, .handler = NULL},
    {"^", ONDA_OP_BITWISE_XOR, .handler = NULL},
    {"~", ONDA_OP_BITWISE_NOT, .handler = NULL},
    {"not", ONDA_OP_NOT, .handler = NULL},
    {"==", ONDA_OP_EQ, .handler = NULL},
    {"!=", ONDA_OP_NEQ, .handler = NULL},
    {"<", ONDA_OP_LT, .handler = NULL},
    {"<=", ONDA_OP_LTE, .handler = NULL},
    {">", ONDA_OP_GT, .handler = NULL},
    {">=", ONDA_OP_GTE, .handler = NULL},
    {"and", ONDA_OP_AND, .handler = NULL},
    {"or", ONDA_OP_OR, .handler = NULL},
    {"drop", ONDA_OP_DROP, .handler = NULL},
    {"dup", ONDA_OP_DUP, .handler = NULL},
    {"over", ONDA_OP_OVER, .handler = NULL},
    {"rot", ONDA_OP_ROT, .handler = NULL},
    {"swap", ONDA_OP_SWAP, .handler = NULL},
    {"ret", ONDA_OP_RET, .handler = NULL},
    {"b@", ONDA_OP_PUSH_FROM_ADDR_B, .handler = NULL},
    {"b!", ONDA_OP_STORE_TO_ADDR_B, .handler = NULL},
    {"h@", ONDA_OP_PUSH_FROM_ADDR_HW, .handler = NULL},
    {"h!", ONDA_OP_STORE_TO_ADDR_HW, .handler = NULL},
    {"w@", ONDA_OP_PUSH_FROM_ADDR_W, .handler = NULL},
    {"w!", ONDA_OP_STORE_TO_ADDR_W, .handler = NULL},
    {"@", ONDA_OP_PUSH_FROM_ADDR_DW, .handler = NULL},
    {"!", ONDA_OP_STORE_TO_ADDR_DW, .handler = NULL},
    {"->", .handler = onda_compile_store_local},
    {"if", .handler = onda_compile_if},
    {"while", .handler = onda_compile_while},
    {"continue", .handler = onda_compile_continue},
    {"import", .handler = onda_compile_import},
};
static const size_t num_imm_words = sizeof(imm_words) / sizeof(imm_words[0]);

static inline const onda_imm_word_t* find_imm_word(const char* name,
                                                   size_t len) {
  for (size_t i = 0; i < num_imm_words; i++) {
    if (strlen(imm_words[i].name) == len &&
        strncmp(imm_words[i].name, name, len) == 0)
      return &imm_words[i];
  }
  return NULL;
}

static int validate_name_availability(onda_lexer_t* lexer,
                                      onda_env_t* env,
                                      onda_code_obj_t* cobj,
                                      const onda_token_t* tok,
                                      const char* symbol_kind) {
  if (tok->len >= ONDA_MAX_WORD_NAME_LEN) {
    print_err(lexer,
              "%s name '%.*s' exceeds max length of %d",
              symbol_kind,
              tok->len,
              tok->start,
              ONDA_MAX_WORD_NAME_LEN - 1);
    return -1;
  }

  for (size_t i = 0; i < tok->len; i++) {
    const char ch = tok->start[i];
    if (ch == '(' || ch == ')' || ch == '|' || ch == ':' || ch == ';') {
      print_err(lexer,
                "%s name '%.*s' contains reserved character '%c'\n",
                symbol_kind,
                tok->len,
                tok->start,
                ch);
      return -1;
    }
  }

  if (find_imm_word(tok->start, tok->len)) {
    print_err(lexer,
              "%s name '%.*s' conflicts with immediate word name\n",
              symbol_kind,
              tok->len,
              tok->start);
    return -1;
  }

  if (onda_native_fn_get(&env->native_registry, tok->start, tok->len)) {
    print_err(lexer,
              "%s name '%.*s' conflicts with builtin name\n",
              symbol_kind,
              tok->len,
              tok->start);
    return -1;
  }

  uint64_t id;
  if (onda_dict_get(&cobj->words_map, tok->start, tok->len, &id) == 0) {
    print_err(lexer,
              "%s name '%.*s' conflicts with word name\n",
              symbol_kind,
              tok->len,
              tok->start);
    return -1;
  }
  if (onda_dict_get(&cobj->aliases_map, tok->start, tok->len, &id) == 0) {
    print_err(lexer,
              "%s name '%.*s' conflicts with alias name\n",
              symbol_kind,
              tok->len,
              tok->start);
    return -1;
  }

  return 0;
}

static int validate_symbol_name(onda_lexer_t* lexer,
                                onda_env_t* env,
                                onda_code_obj_t* cobj,
                                const onda_token_t* tok,
                                bool is_alias) {
  return validate_name_availability(
      lexer, env, cobj, tok, is_alias ? "Alias" : "Word");
}

static int onda_compile_word(onda_lexer_t* lexer,
                             onda_env_t* env,
                             onda_code_obj_t* cobj) {
  onda_word_t word = {0};
  onda_token_t tok;
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_IDENTIFIER) {
    print_err(lexer, "Expected word name after ':'");
    return -1;
  }
  if (validate_symbol_name(lexer, env, cobj, &tok, false) != 0)
    return -1;

  if (strncmp(tok.start, "main", tok.len) == 0)
    cobj->entry_pc = cobj->size;

  // Make word
  strncpy(word.name, tok.start, (size_t)tok.len);
  word.name_len = (size_t)tok.len;
  word.pc = cobj->size;
  onda_dict_put(&cobj->words_map, word.name, word.name_len, cobj->words_count);
  cobj->words =
      realloc(cobj->words, (cobj->words_count + 1) * sizeof(onda_word_t));

  // new scope for word locals
  onda_scope_push(cobj);

  // Parse arguments if any
  bool is_argument_section = true;
  onda_token_peek(lexer, &tok);
  if (tok.type == TOKEN_LPAREN) {
    onda_token_next(lexer, &tok); // consume '('
    do {
      onda_token_peek(lexer, &tok);
      if (tok.type == TOKEN_RPAREN) {
        onda_token_next(lexer, &tok); // consume ')'
        break;
      } else if (tok.type == TOKEN_IDENTIFIER && tok.len == 1 &&
                 tok.start[0] == '|') {
        onda_token_next(lexer, &tok); // consume '|'
        is_argument_section = false;
        continue;
      }
      if (tok.type != TOKEN_IDENTIFIER) {
        print_err(lexer,
                  "Expected word argument name in word definition for '%.*s'",
                  (int)word.name_len,
                  word.name);
        return -1;
      }
      onda_token_next(lexer, &tok); // consume argument name
      if (validate_name_availability(lexer, env, cobj, &tok, "Local") != 0)
        return -1;
      if (onda_scope_set(cobj, tok.start, tok.len, word.locals_count++) != 0) {
        print_err(
            lexer,
            "Failed to define local variable '%.*s' in word definition for "
            "word '%.*s' \n",
            tok.len,
            tok.start,
            (int)word.name_len,
            word.name);
        return -1;
      }
      if (is_argument_section)
        word.args_count++;
    } while (true);
  }

  // Store new word
  cobj->words[cobj->words_count++] = word;

  // Compile word body until ';'
  do {
    onda_token_peek(lexer, &tok);
    if (tok.type == TOKEN_SEMICOLON) {
      CODE_PUSH_OPCODE(ONDA_OP_RET);
      onda_token_next(lexer, &tok); // consume ';'
      break;
    }
    if (tok.type == TOKEN_COLON) {
      print_err(lexer,
                "Nested word definition not allowed for word '%.*s'",
                tok.len,
                tok.start);
      return -1;
    }
    int rc = onda_compile_expr(lexer, env, cobj);
    if (rc != 0)
      return rc;
  } while (true);

  onda_scope_pop(cobj); // pop word scope

  return 0;
}

static int onda_compile_alias(onda_lexer_t* lexer,
                              onda_env_t* env,
                              onda_code_obj_t* cobj) {
  (void)env; // unused arg
  onda_alias_t alias = {0};
  onda_token_t tok;
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_IDENTIFIER) {
    print_err(lexer, "Expected alias name after '::'");
    return -1;
  }
  if (validate_symbol_name(lexer, env, cobj, &tok, true) != 0)
    return -1;

  strncpy(alias.name, tok.start, (size_t)tok.len);
  alias.name_len = (size_t)tok.len;

  const size_t body_start = lexer->pos;
  size_t body_end = body_start;
  while (true) {
    onda_token_next(lexer, &tok);
    if (tok.type == TOKEN_EOF) {
      print_err(lexer,
                "Unterminated alias definition '%.*s'",
                (int)alias.name_len,
                alias.name);
      return -1;
    }
    if (tok.type == TOKEN_SEMICOLON) {
      body_end = (size_t)(tok.start - lexer->src);
      break;
    }
    if (tok.type == TOKEN_LPAREN || tok.type == TOKEN_RPAREN ||
        (tok.type == TOKEN_IDENTIFIER && tok.len == 1 && tok.start[0] == '|')) {
      print_err(lexer,
                "Alias '%.*s' cannot declare arguments or local variables\n",
                (int)alias.name_len,
                alias.name);
      return -1;
    }
  }

  const size_t body_len = body_end - body_start;
  alias.body_src = onda_malloc(body_len + 1);
  memcpy(alias.body_src, lexer->src + body_start, body_len);
  alias.body_src[body_len] = '\0';

  cobj->aliases =
      realloc(cobj->aliases, (cobj->aliases_count + 1) * sizeof(onda_alias_t));
  cobj->aliases[cobj->aliases_count] = alias;
  onda_dict_put(&cobj->aliases_map,
                alias.name,
                alias.name_len,
                cobj->aliases_count);
  cobj->aliases_count++;

  return 0;
}

static int onda_compile_expr(onda_lexer_t* lexer,
                             onda_env_t* env,
                             onda_code_obj_t* cobj) {
  onda_token_t tok;
  onda_token_next(lexer, &tok);

  switch (tok.type) {
  case TOKEN_COLON:
    onda_token_peek(lexer, &tok);
    if (tok.type == TOKEN_COLON) {
      onda_token_next(lexer, &tok); // consume second ':'
      return onda_compile_alias(lexer, env, cobj);
    }
    return onda_compile_word(lexer, env, cobj);
  case TOKEN_IDENTIFIER: {
    // Is it an immediate word
    const onda_imm_word_t* imm_word = find_imm_word(tok.start, tok.len);
    if (imm_word) {
      if (imm_word->handler)
        return imm_word->handler(lexer, env, cobj);
      CODE_PUSH_OPCODE(imm_word->opcode);
      (void)onda_try_optimize(cobj);
      return 0;
    }

    // Is it an alias?
    uint64_t alias_id;
    if (onda_dict_get(&cobj->aliases_map, tok.start, tok.len, &alias_id) == 0) {
      if (cobj->alias_expand_depth >= ONDA_MAX_IMPORT_DEPTH) {
        print_err(lexer,
                  "Maximum alias expansion depth (%d) exceeded on '%.*s'\n",
                  ONDA_MAX_IMPORT_DEPTH,
                  tok.len,
                  tok.start);
        return -1;
      }
      cobj->alias_expand_depth++;
      onda_lexer_t alias_lexer = *lexer;
      alias_lexer.src = cobj->aliases[alias_id].body_src;
      alias_lexer.pos = 0;
      alias_lexer.line = 0;
      alias_lexer.column = 0;
      const int rc = onda_compile(&alias_lexer, env, cobj);
      cobj->alias_expand_depth--;
      return rc;
    }

    // Is it a defined word?
    uint64_t word_id;
    if (onda_dict_get(&cobj->words_map, tok.start, tok.len, &word_id) == 0) {
      CODE_PUSH_OPCODE(ONDA_OP_CALL);
      CODE_PUSH_BYTE(cobj->words[word_id].args_count);
      CODE_PUSH_BYTE(cobj->words[word_id].locals_count);
      const size_t call_pc = cobj->size;
      CODE_PUSH_BYTES(&word_id, sizeof(int32_t));
      const size_t next_instr_pc = cobj->size;
      const int32_t offset =
          ((int32_t)cobj->words[word_id].pc - (int32_t)next_instr_pc);
      memcpy(&cobj->code[call_pc], &offset, sizeof(int32_t));
      return 0;
    }

    // Is it a local variable?
    uint8_t local_id;
    if (onda_scope_get(cobj, tok.start, tok.len, &local_id) == 0) {
      CODE_PUSH_OPCODE(ONDA_OP_PUSH_LOCAL);
      CODE_PUSH_BYTE(local_id + ONDA_LOCALS_BASE_OFF);
      return 0;
    }

    // Is it a native function call?
    uint64_t func_id;
    if (onda_dict_get(&env->native_registry.items_map,
                      tok.start,
                      tok.len,
                      &func_id) == 0) {
      CODE_PUSH_OPCODE(ONDA_OP_CALL_NATIVE);
      uint32_t idx = (uint32_t)func_id;
      CODE_PUSH_BYTES(&idx, sizeof(uint32_t));
      return 0;
    }

    print_err(lexer, "Unknown identifier '%.*s'\n", tok.len, tok.start);
    return -1; // unknown identifier
  }
  case TOKEN_NUMBER:
    if (tok.number >= 0 && tok.number <= 0x7F) {
      CODE_PUSH_OPCODE(ONDA_OP_PUSH_CONST_U8);
      CODE_PUSH_BYTE((int8_t)tok.number);
    } else if ((tok.number >= 0) && (tok.number <= INT32_MAX)) {
      CODE_PUSH_OPCODE(ONDA_OP_PUSH_CONST_U32);
      const uint32_t val = (int32_t)(tok.number);
      CODE_PUSH_BYTES(&val, sizeof(int32_t));
    } else {
      CODE_PUSH_OPCODE(ONDA_OP_PUSH_CONST_U64);
      const uint64_t val = (int64_t)(tok.number);
      CODE_PUSH_BYTES(&val, sizeof(int64_t));
    }
    break;
  case TOKEN_STRING: {
    uint32_t offset = 0;
    if (code_pool_push_string(cobj, tok.start, tok.len, &offset) != 0)
      return -1;
    CODE_PUSH_OPCODE(ONDA_OP_PUSH_CONST_POOL_PTR_U32);
    CODE_PUSH_BYTES(&offset, sizeof(uint32_t));
    break;
  }
  case TOKEN_EOF:
    return 0;
  default:
    print_err(lexer,
              "Unexpected token '%.s' in expression",
              tok.len > 0 ? tok.start : "<EOF>");
    return -1;
  }
  return 0;
}

int onda_compile(onda_lexer_t* lexer,
                 onda_env_t* env,
                 onda_code_obj_t* code_obj) {
  while (true) {
    if (at_end(lexer))
      break;
    int rc = onda_compile_expr(lexer, env, code_obj);
    if (rc != 0)
      return rc;
  }
  return 0;
}

int onda_compile_file(const char* filepath,
                      onda_lexer_t* lexer,
                      onda_env_t* env,
                      onda_code_obj_t* cobj) {
  char filename[ONDA_MAX_FILENAME_LEN];
  char resolved_path[ONDA_MAX_FILEPATH_LEN];
  char* import_src = NULL;
  int rc = -1;
  bool pushed_import = false;
  // Extract filename
  const char* last_slash = strrchr(filepath, '/');
  if (last_slash)
    strncpy(filename, last_slash + 1, sizeof(filename) - 1);
  else
    strncpy(filename, filepath, sizeof(filename) - 1);
  filename[sizeof(filename) - 1] = '\0';

  // Backup previous lexer state to restore after import
  const char* prev_filepath = lexer->filepath;
  const char* prev_filename = lexer->filename;
  const char* prev_src = lexer->src;
  size_t prev_column = lexer->column, prev_line = lexer->line,
         prev_pos = lexer->pos;

  // Extract current file base path for resolving relative imports
  resolved_path[0] = '\0';
  if (lexer->filepath) {
    const char* last_slash_for_base = strrchr(lexer->filepath, '/');
    if (last_slash_for_base) {
      const size_t basepath_len = last_slash_for_base - lexer->filepath + 1;
      memcpy(resolved_path, lexer->filepath, basepath_len);
      resolved_path[basepath_len] = '\0';
    }
  }

  // Append import path to base path
  if (strlen(resolved_path) + strlen(filepath) >= sizeof(resolved_path)) {
    print_err(
        lexer,
        "Resolved import path too long for file: %s (max %zu characters)\n",
        filepath,
        sizeof(resolved_path) - 1);
    goto cleanup;
  }
  strcat(resolved_path, filepath);

  if (lexer->import_depth >= ONDA_MAX_IMPORT_DEPTH) {
    print_err(lexer,
              "Maximum import depth (%d) exceeded while importing: %s\n",
              ONDA_MAX_IMPORT_DEPTH,
              resolved_path);
    goto cleanup;
  }
  for (size_t i = 0; i < lexer->import_depth; i++) {
    if (strcmp(lexer->import_stack[i], resolved_path) == 0) {
      print_err(lexer, "Import cycle detected with file: %s\n", resolved_path);
      goto cleanup;
    }
  }
  lexer->import_stack[lexer->import_depth++] = resolved_path;
  pushed_import = true;

  // Read imported file
  size_t import_size;
  import_src = read_file(resolved_path, &import_size);
  if (!import_src || import_size == 0) {
    print_err(lexer, "Failed to read file: %s\n", resolved_path);
    goto cleanup;
  }

  // Set lexer state to imported file and compile
  lexer->filepath = resolved_path;
  lexer->filename = filename;
  lexer->src = import_src;
  lexer->pos = 0;
  lexer->line = 0;
  lexer->column = 0;
  rc = onda_compile(lexer, env, cobj);
  if (rc != 0) {
    print_err(lexer, "Compilation error in imported file: %s\n", resolved_path);
    goto cleanup;
  }
  rc = 0;

cleanup:
  if (import_src)
    onda_free(import_src);
  if (pushed_import)
    lexer->import_depth--;
  lexer->filepath = prev_filepath;
  lexer->filename = prev_filename;
  lexer->src = prev_src;
  lexer->column = prev_column;
  lexer->line = prev_line;
  lexer->pos = prev_pos;

  return rc;
}

int onda_code_obj_init(onda_code_obj_t* cobj, size_t initial_capacity) {
  cobj->code = (uint8_t*)onda_malloc(initial_capacity);
  if (!cobj->code)
    return -1;
  cobj->size = 0;
  cobj->capacity = initial_capacity;
  cobj->entry_pc = 0;
  cobj->const_pool = NULL;
  cobj->const_pool_size = 0;
  cobj->const_pool_capacity = 0;
  onda_dict_init(&cobj->words_map);
  onda_dict_init(&cobj->aliases_map);
  cobj->words = NULL;
  cobj->words_count = 0;
  cobj->aliases = NULL;
  cobj->aliases_count = 0;
  cobj->alias_expand_depth = 0;
  cobj->inner_loop_start_pc = -1;
  cobj->recent_opcode_count = 0;
  return 0;
}

void onda_code_obj_free(onda_code_obj_t* cobj) {
  if (cobj->code)
    onda_free(cobj->code);
  if (cobj->const_pool)
    onda_free(cobj->const_pool);
  onda_dict_free(&cobj->words_map);
  onda_dict_free(&cobj->aliases_map);
  if (cobj->words)
    free(cobj->words);
  if (cobj->aliases) {
    for (size_t i = 0; i < cobj->aliases_count; i++)
      onda_free(cobj->aliases[i].body_src);
    free(cobj->aliases);
  }
  memset(cobj, 0, sizeof(onda_code_obj_t));
}
