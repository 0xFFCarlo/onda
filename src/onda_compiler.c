#include "onda_compiler.h"

#include "onda_dict.h"
#include "onda_util.h"
#include "onda_vm.h"

#include <ctype.h>
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
static int lex_string(onda_lexer_t* lx, const char** dst, int* dst_len) {
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

static int64_t parse_number(const char* s, size_t len) {
  char* tmp = (char*)onda_malloc(len + 1);
  if (!tmp)
    return 0.0;
  memcpy(tmp, s, len);
  tmp[len] = '\0';
  char* endptr = NULL;
  int64_t v = strtoll(tmp, &endptr, 10);
  onda_free(tmp);
  return v;
}

static int lex_number(onda_lexer_t* lx) {
  size_t start = lx->pos;

  if (curr(lx) == '-' && isdigit((unsigned char)nextc(lx)))
    advance(lx);

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
    t->number = parse_number(t->start, t->len);
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

void onda_token_peek(onda_lexer_t* lexer, onda_token_t* t) {
  size_t saved_pos = lexer->pos;
  size_t saved_line = lexer->line;
  size_t saved_column = lexer->column;
  onda_token_next(lexer, t);
  lexer->pos = saved_pos;
  lexer->line = saved_line;
  lexer->column = saved_column;
}

// Helper macros for emitting bytecode with bounds checking
#define CODE_CHECK_SPACE(bytes_needed)                                         \
  do {                                                                         \
    if (cobj->size + (bytes_needed) > cobj->capacity) {                        \
      fprintf(stderr, "Code buffer overflow\n");                               \
      return -1;                                                               \
    }                                                                          \
  } while (0)
#define CODE_PUSH_BYTE(val)                                                    \
  CODE_CHECK_SPACE(1);                                                         \
  cobj->code[cobj->size++] = (val)
#define CODE_PUSH_BYTES(src, len)                                              \
  do {                                                                         \
    CODE_CHECK_SPACE(len);                                                     \
    memcpy(&cobj->code[cobj->size], (src), (len));                             \
    cobj->size += (len);                                                       \
  } while (0)

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

static int onda_compile_expr(onda_lexer_t* lexer, onda_code_obj_t* cobj);

static inline int onda_compile_until_ident(onda_lexer_t* lexer,
                                           onda_code_obj_t* cobj,
                                           const char* ident_a,
                                           const size_t ident_a_len,
                                           const char* ident_b,
                                           const size_t ident_b_len) {
  onda_token_t tok;
  while (true) {
    onda_token_peek(lexer, &tok);
    if (tok.type == TOKEN_IDENTIFIER) {
      if (tok.len == ident_a_len &&
          strncmp(tok.start, ident_a, ident_a_len) == 0) {
        onda_token_next(lexer, &tok); // consume ident_a
        return 0;
      }
      if (ident_b && tok.len == ident_b_len &&
          strncmp(tok.start, ident_b, ident_b_len) == 0) {
        onda_token_next(lexer, &tok); // consume ident_b
        return 1;
      }
    }
    int rc = onda_compile_expr(lexer, cobj);
    if (rc != 0)
      return rc;
  }
  return -1; // should never reach
}

static int onda_compile_if(onda_lexer_t* lexer, onda_code_obj_t* cobj) {
  int rc;
  rc = onda_compile_until_ident(lexer, cobj, "then", 4, NULL, 0);
  if (rc < 0)
    return rc;

  CODE_PUSH_BYTE(ONDA_OP_JUMP_IF_FALSE);
  size_t condition_jmp_pc = cobj->size;
  CODE_PUSH_BYTES(&(int16_t){0},
                  sizeof(int16_t)); // placeholder for jump offset
  rc = onda_compile_until_ident(lexer, cobj, "else", 4, "end", 5);
  if (rc < 0)
    return rc;

  // If we ended on "else", we need to emit a jump over the else block
  size_t then_jmp_pc;
  if (rc == 0) {
    CODE_PUSH_BYTE(ONDA_OP_JUMP);
    then_jmp_pc = cobj->size;
    CODE_PUSH_BYTES(&(int16_t){0},
                    sizeof(int16_t)); // placeholder for jump offset
  }

  // Patch the jump offset for the condition
  size_t after_then_pc = cobj->size;
  int16_t condition_jmp_offset = (int16_t)(after_then_pc - condition_jmp_pc);
  memcpy(&cobj->code[condition_jmp_pc], &condition_jmp_offset, sizeof(int16_t));

  // If we ended on "else", we need to compile the else block too
  if (rc == 0) {
    rc = onda_compile_until_ident(lexer, cobj, "end", 5, NULL, 0);
    if (rc < 0)
      return rc;

    // Patch the jump offset for the then block
    size_t after_else_pc = cobj->size;
    int16_t then_jmp_offset = (int16_t)(after_else_pc - then_jmp_pc);
    memcpy(&cobj->code[then_jmp_pc], &then_jmp_offset, sizeof(int16_t));
  }

  return 0;
}

static int onda_compile_while(onda_lexer_t* lexer, onda_code_obj_t* cobj) {
  size_t loop_start_pc = cobj->size;
  int rc = onda_compile_until_ident(lexer, cobj, "do", 2, NULL, 0);
  if (rc < 0)
    return rc;

  CODE_PUSH_BYTE(ONDA_OP_JUMP_IF_FALSE);
  size_t condition_jmp_pc = cobj->size;
  CODE_PUSH_BYTES(&(int16_t){0},
                  sizeof(int16_t)); // placeholder for jump offset

  rc = onda_compile_until_ident(lexer, cobj, "end", 8, NULL, 0);
  if (rc < 0)
    return rc;

  // Emit jump back to loop start
  CODE_PUSH_BYTE(ONDA_OP_JUMP);
  size_t loop_end_jmp_pc = cobj->size;
  const int16_t loop_back_offset = (int16_t)(loop_start_pc - loop_end_jmp_pc);
  CODE_PUSH_BYTES(&loop_back_offset, sizeof(int16_t));

  // Patch the jump offset for the condition
  size_t after_loop_pc = cobj->size;
  int16_t condition_jmp_offset = (int16_t)(after_loop_pc - condition_jmp_pc);
  memcpy(&cobj->code[condition_jmp_pc], &condition_jmp_offset, sizeof(int16_t));

  return 0;
}

static int onda_compile_store_local(onda_lexer_t* lexer,
                                    onda_code_obj_t* cobj) {
  onda_token_t tok;
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_IDENTIFIER) {
    fprintf(stderr,
            "Expected local variable name after '->' at line %lu, column %lu\n",
            lexer->line,
            lexer->column);
    return -1;
  }
  uint8_t local_id;
  if (onda_scope_get(cobj, tok.start, tok.len, &local_id) != 0) {
    fprintf(stderr,
            "Undefined variable '%.*s' in store operation at line %lu, column "
            "%lu\n",
            tok.len,
            tok.start,
            lexer->line,
            lexer->column);
    return -1;
  }
  CODE_PUSH_BYTE(ONDA_OP_STORE_LOCAL);
  CODE_PUSH_BYTE(local_id + ONDA_LOCALS_BASE_OFF);
  return 0;
}

typedef int (*imm_word_handler_t)(onda_lexer_t* lexer, onda_code_obj_t* cobj);

typedef struct {
  const char* name;
  uint8_t opcode;
  imm_word_handler_t handler;
} onda_imm_word_t;

static const onda_imm_word_t imm_words[] = {
    // Operators
    {"+", ONDA_OP_ADD},
    {"-", ONDA_OP_SUB},
    {"*", ONDA_OP_MUL},
    {"/", ONDA_OP_DIV},
    {"%", ONDA_OP_MOD},
    {"++", ONDA_OP_INC},
    {"--", ONDA_OP_DEC},
    {"!", ONDA_OP_NOT},
    {"==", ONDA_OP_EQ},
    {"!=", ONDA_OP_NEQ},
    {"<", ONDA_OP_LT},
    {"<=", ONDA_OP_LTE},
    {">", ONDA_OP_GT},
    {">=", ONDA_OP_GTE},
    {"and", ONDA_OP_AND},
    {"or", ONDA_OP_OR},
    {"drop", ONDA_OP_DROP},
    {"dup", ONDA_OP_DUP},
    {"over", ONDA_OP_OVER},
    {"rot", ONDA_OP_ROT},
    {"swap", ONDA_OP_SWAP},
    {"print", ONDA_OP_PRINT},
    {"prints", ONDA_OP_PRINT_STR},
    {"ret", ONDA_OP_RET},
    {"@", ONDA_OP_PUSH_FROM_ADDR},
    {"!", ONDA_OP_STORE_TO_ADDR},
    {"->", .handler = onda_compile_store_local},
    {"if", .handler = onda_compile_if},
    {"while", .handler = onda_compile_while},
};
static const size_t num_imm_words = sizeof(imm_words) / sizeof(imm_words[0]);

static int onda_compile_word(onda_lexer_t* lexer, onda_code_obj_t* cobj) {
  onda_word_t word = {0};
  onda_token_t tok;
  onda_token_next(lexer, &tok);
  if (tok.type != TOKEN_IDENTIFIER) {
    fprintf(stderr,
            "Expected word name after ':' at line %lu, column %lu\n",
            lexer->line,
            lexer->column);
    return -1;
  }

  if (tok.len >= ONDA_MAX_WORD_LEN) {
    fprintf(stderr,
            "Word name '%.*s' at line %lu, column %lu exceeds max length of "
            "%d\n",
            tok.len,
            tok.start,
            lexer->line,
            lexer->column,
            ONDA_MAX_WORD_LEN - 1);
  }

  // Check that word definition does not match any immediate word
  for (size_t i = 0; i < num_imm_words; i++) {
    if (strlen(imm_words[i].name) == (size_t)tok.len &&
        strncmp(imm_words[i].name, tok.start, (size_t)tok.len) == 0) {
      fprintf(stderr,
              "Word name '%.*s' at line %lu, column %lu conflicts with "
              "immediate word name\n",
              tok.len,
              tok.start,
              lexer->line,
              lexer->column);
      return -1;
    }
  }

  // Check that word definition does not already exists
  uint64_t word_id;
  if (onda_dict_get(&cobj->words_map, tok.start, tok.len, &word_id) == 0) {
    fprintf(stderr,
            "Word name '%.*s' at line %lu, column %lu already defined\n",
            tok.len,
            tok.start,
            lexer->line,
            lexer->column);
    return -1;
  }

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
  onda_token_peek(lexer, &tok);
  if (tok.type == TOKEN_LPAREN) {
    onda_token_next(lexer, &tok); // consume '('
    do {
      onda_token_peek(lexer, &tok);
      if (tok.type == TOKEN_RPAREN) {
        onda_token_next(lexer, &tok); // consume ')'
        break;
      }
      if (tok.type != TOKEN_IDENTIFIER) {
        fprintf(
            stderr,
            "Expected word argument name in word definition for word '%.*s' at "
            "line %lu, column %lu\n",
            (int)word.name_len,
            word.name,
            lexer->line,
            lexer->column);
        return -1;
      }
      onda_token_next(lexer, &tok); // consume argument name
      if (onda_scope_set(cobj, tok.start, tok.len, word.locals_count++) != 0) {
        fprintf(stderr,
                "Failed to define argument '%.*s' in word definition for word "
                "'%.*s' at line %lu, column %lu\n",
                tok.len,
                tok.start,
                (int)word.name_len,
                word.name,
                lexer->line,
                lexer->column);
        return -1;
      }
      word.args_count++;
    } while (true);
  }

  // Store new word
  cobj->words[cobj->words_count++] = word;

  // Compile word body until ';'
  do {
    onda_token_peek(lexer, &tok);
    if (tok.type == TOKEN_SEMICOLON) {
      CODE_PUSH_BYTE(ONDA_OP_RET);
      onda_token_next(lexer, &tok); // consume ';'
      break;
    }
    if (tok.type == TOKEN_COLON) {
      fprintf(stderr,
              "Nested word definition not allowed for word '%.*s' at line "
              "%lu, column %lu\n",
              tok.len,
              tok.start,
              lexer->line,
              lexer->column);
      return -1;
    }
    int rc = onda_compile_expr(lexer, cobj);
    if (rc != 0)
      return rc;
  } while (true);

  onda_scope_pop(cobj); // pop word scope

  return 0;
}

static int onda_compile_expr(onda_lexer_t* lexer, onda_code_obj_t* cobj) {

  onda_token_t tok;
  onda_token_next(lexer, &tok);

  switch (tok.type) {
  case TOKEN_COLON:
    return onda_compile_word(lexer, cobj);
    break;
  case TOKEN_IDENTIFIER:
    // Is it an immediate word
    for (size_t i = 0; i < num_imm_words; i++) {
      if (strlen(imm_words[i].name) != (size_t)tok.len ||
          strncmp(imm_words[i].name, tok.start, (size_t)tok.len) != 0)
        continue;
      if (imm_words[i].handler) {
        return imm_words[i].handler(lexer, cobj);
      } else {
        CODE_PUSH_BYTE(imm_words[i].opcode);
        return 0;
      }
    }

    // Is it a defined word?
    uint64_t word_id;
    if (onda_dict_get(&cobj->words_map, tok.start, tok.len, &word_id) == 0) {
      CODE_PUSH_BYTE(ONDA_OP_CALL);
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

    // Is it a local variable
    uint8_t local_id;
    if (onda_scope_get(cobj, tok.start, tok.len, &local_id) == 0) {
      CODE_PUSH_BYTE(ONDA_OP_PUSH_LOCAL);
      CODE_PUSH_BYTE(local_id + ONDA_LOCALS_BASE_OFF);
      return 0;
    }

    // TODO: Is it a C function call?
    printf("Unknown identifier '%.*s' at line %lu, column %lu\n",
           tok.len,
           tok.start,
           lexer->line,
           lexer->column);
    return -1; // unknown identifier
  case TOKEN_NUMBER:
    if (tok.number <= 0x7F && tok.number >= -0x80) {
      CODE_PUSH_BYTE(ONDA_OP_PUSH_CONST_U8);
      CODE_PUSH_BYTE((int8_t)tok.number);
    } else if ((tok.number <= INT32_MAX) && (tok.number >= INT32_MIN)) {
      CODE_PUSH_BYTE(ONDA_OP_PUSH_CONST_U32);
      const uint32_t val = (int32_t)(tok.number);
      CODE_PUSH_BYTES(&val, sizeof(int32_t));
    } else {
      CODE_PUSH_BYTE(ONDA_OP_PUSH_CONST_U64);
      const uint64_t val = (int64_t)(tok.number);
      CODE_PUSH_BYTES(&val, sizeof(int64_t));
    }
    break;
  case TOKEN_STRING: {
    // TODO: constants should be stored in the
    // bytecode somehow
    char* str_data = onda_malloc(tok.len + 1);
    memcpy(str_data, tok.start, tok.len);
    str_data[tok.len] = '\0';
    CODE_PUSH_BYTE(ONDA_OP_PUSH_CONST_U64);
    const uint64_t addr = (uint64_t)(uintptr_t)str_data;
    CODE_PUSH_BYTES(&addr, sizeof(uint64_t));
    break;
  }
  case TOKEN_EOF:
    return 0;
  default:
    fprintf(stderr,
            "Unexpected token '%.s' in expression at line %lu, column %lu\n",
            tok.len > 0 ? tok.start : "<EOF>",
            lexer->line,
            lexer->column);
    return -1;
  }
  return 0;
}

int onda_compile(onda_lexer_t* lexer, onda_code_obj_t* code_obj) {
  code_obj->entry_pc = 0;
  while (true) {
    if (at_end(lexer))
      break;
    int rc = onda_compile_expr(lexer, code_obj);
    if (rc != 0)
      return rc;
  }
  return 0;
}

int onda_code_obj_init(onda_code_obj_t* cobj, size_t initial_capacity) {
  cobj->code = (uint8_t*)onda_malloc(initial_capacity);
  if (!cobj->code)
    return -1;
  cobj->size = 0;
  cobj->capacity = initial_capacity;
  cobj->entry_pc = 0;
  onda_dict_init(&cobj->words_map);
  cobj->words = NULL;
  cobj->words_count = 0;
  return 0;
}

void onda_code_obj_free(onda_code_obj_t* cobj) {
  if (cobj->code)
    onda_free(cobj->code);
  onda_dict_free(&cobj->words_map);
  if (cobj->words)
    free(cobj->words);
  memset(cobj, 0, sizeof(onda_code_obj_t));
}
