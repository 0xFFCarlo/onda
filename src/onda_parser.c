#include "onda_parser.h"

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

static inline void tokop1(onda_lexer_t* l, onda_token_t* t, int subtype) {
  t->type = TOKEN_OPERATOR;
  t->subtype = subtype;
  t->len = 1;
  advance(l);
}

static inline void tokop2_if(onda_lexer_t* l,
                             onda_token_t* t,
                             char expect,
                             int one_subtype,
                             int two_subtype) {
  t->type = TOKEN_OPERATOR;
  t->subtype = one_subtype;
  t->len = 1;
  if (nextc(l) == expect) {
    t->subtype = two_subtype;
    t->len = 2;
    advance(l);
  }
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
  case '@': {
    // label
    advance(lexer); // skip '@'
    size_t start_pos = lexer->pos;
    while (!at_end(lexer) && !isspace((unsigned char)curr(lexer)))
      advance(lexer);
    t->type = TOKEN_LABEL;
    t->start = lexer->src + start_pos;
    t->len = (int)(lexer->pos - start_pos);
    return;
  }
  case '+':
    return tokop2_if(lexer, t, '+', OPERATOR_ADD, OPERATOR_INC);
  case '*':
    return tokop1(lexer, t, OPERATOR_MULTIPLY);
  case '/':
    return tokop1(lexer, t, OPERATOR_DIVIDE);
  case '%':
    return tokop1(lexer, t, OPERATOR_MODULO);
  case '-':
    if (!isdigit((unsigned char)nextc(lexer))) {
      return tokop2_if(lexer, t, '-', OPERATOR_SUBTRACT, OPERATOR_DEC);
    }
    break;
  case '!':
    return tokop2_if(lexer, t, '=', OPERATOR_NOT, OPERATOR_NOT_EQUAL);
  case '=':
    return tokop2_if(lexer,
                     t,
                     '=',
                     /*unused*/ 0,
                     OPERATOR_EQUAL); // see note below
  case '<':
    return tokop2_if(lexer, t, '=', OPERATOR_LESS, OPERATOR_LESS_EQUAL);
  case '>':
    return tokop2_if(lexer, t, '=', OPERATOR_GREATER, OPERATOR_GREATER_EQUAL);
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
}

static int cmp_strptr(const void* a, const void* b) {
  const char* sa = *(const char* const*)a;
  const char* sb = *(const char* const*)b;
  return strcmp(sa, sb);
}

typedef struct {
  const char* name;
  uint8_t opcode;
} onda_keyword_t;

typedef struct {
  const char* s;
  size_t len;
} str_slice_t;

static int cmp_key_kw(const void* a, const void* b) {
  const str_slice_t* k = (const str_slice_t*)a;
  const onda_keyword_t* e = (const onda_keyword_t*)b;

  size_t elen = strlen(e->name);
  size_t m = (k->len < elen) ? k->len : elen;

  int c = memcmp(k->s, e->name, m);
  if (c != 0)
    return c;

  // If one is a prefix of the other, shorter one is "less"
  if (k->len < elen)
    return -1;
  if (k->len > elen)
    return 1;
  return 0;
}

// Used to track unresolved jumps to labels during parsing
typedef struct onda_unresolved_jump_t {
  size_t pc_pos;
  char* label;
  struct onda_unresolved_jump_t* next;
} onda_unresolved_jump_t;

int onda_parse(const char* source,
               uint8_t* code,
               size_t* code_size,
               size_t* entry_pc) {
  onda_lexer_t lexer = {
      .src = source,
      .pos = 0,
      .line = 1,
      .column = 0,
  };
  onda_dict_init(&lexer.words);
  onda_dict_init(&lexer.labels);
  onda_unresolved_jump_t* unresolved_jumps = NULL;
  int rc = 0;

  static const uint8_t oper_to_code_map[] = {
      [OPERATOR_ADD] = ONDA_OP_ADD,
      [OPERATOR_SUBTRACT] = ONDA_OP_SUB,
      [OPERATOR_MULTIPLY] = ONDA_OP_MUL,
      [OPERATOR_DIVIDE] = ONDA_OP_DIV,
      [OPERATOR_MODULO] = ONDA_OP_MOD,
      [OPERATOR_INC] = ONDA_OP_INC,
      [OPERATOR_DEC] = ONDA_OP_DEC,
      [OPERATOR_NOT] = ONDA_OP_NOT,
      [OPERATOR_EQUAL] = ONDA_OP_EQ,
      [OPERATOR_NOT_EQUAL] = ONDA_OP_NEQ,
      [OPERATOR_LESS] = ONDA_OP_LT,
      [OPERATOR_LESS_EQUAL] = ONDA_OP_LTE,
      [OPERATOR_GREATER] = ONDA_OP_GT,
      [OPERATOR_GREATER_EQUAL] = ONDA_OP_GTE,
  };

  static const onda_keyword_t keywords[] = {
      {".", ONDA_OP_PRINT},
      {".s", ONDA_OP_PRINT_STR},
      {"and", ONDA_OP_AND},
      {"drop", ONDA_OP_DROP},
      {"dup", ONDA_OP_DUP},
      {"jmp", ONDA_OP_JUMP},
      {"jmp_if", ONDA_OP_JUMP_IF},
      {"or", ONDA_OP_OR},
      {"over", ONDA_OP_OVER},
      {"ret", ONDA_OP_RET},
      {"rot", ONDA_OP_ROT},
      {"swap", ONDA_OP_SWAP},
  };

  size_t pc = 0;
  while (true) {
    onda_token_t tok;
    onda_token_next(&lexer, &tok);
    if (tok.type == TOKEN_EOF) {
      break;
    } else if (tok.type == TOKEN_INVALID) {
      fprintf(stderr,
              "Lexer error at line %lu, column %lu\n",
              lexer.line,
              lexer.column);
      rc = -1;
      goto done;
    }

    switch (tok.type) {
    case TOKEN_NUMBER:
      if (tok.number <= 0x7F && tok.number >= -0x80) {
        if (pc + 2 > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          rc = -1;
          goto done;
        }
        code[pc++] = ONDA_OP_PUSH_CONST_U8;
        code[pc++] = (int8_t)tok.number;
      } else if (tok.number <= 0x7FFFFFFF && tok.number >= -0x80000000) {
        if (pc + 1 + sizeof(int32_t) > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          rc = -1;
          goto done;
        }
        code[pc++] = ONDA_OP_PUSH_CONST_U32;
        uint32_t val = (int32_t)(tok.number);
        memcpy(&code[pc], &val, sizeof(uint32_t));
        pc += sizeof(int32_t);
      } else {
        if (pc + 1 + sizeof(int64_t) > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          rc = -1;
          goto done;
        }
        code[pc++] = ONDA_OP_PUSH_CONST_U64;
        uint64_t val = (int64_t)(tok.number);
        memcpy(&code[pc], &val, sizeof(int64_t));
        pc += sizeof(int64_t);
      }
      break;
    case TOKEN_OPERATOR:
      if (pc + 1 > *code_size) {
        fprintf(stderr, "Code buffer overflow\n");
        rc = -1;
        goto done;
      }
      code[pc++] = oper_to_code_map[tok.subtype];
      break;
    case TOKEN_STRING: {
      // TODO: constants should be stored in the
      // bytecode somehow
      char* str_data = onda_malloc(tok.len + 1);
      memcpy(str_data, tok.start, tok.len);
      str_data[tok.len] = '\0';
      code[pc++] = ONDA_OP_PUSH_CONST_U64;
      uint64_t addr = (uint64_t)(uintptr_t)str_data;
      memcpy(&code[pc], &addr, sizeof(uint64_t));
      pc += sizeof(uint64_t);
      break;
    }
    case TOKEN_IDENTIFIER: {
      if (pc + 1 > *code_size) {
        fprintf(stderr, "Code buffer overflow\n");
        rc = -1;
        goto done;
      }
      str_slice_t k = {.s = tok.start, .len = tok.len};

      // Check if it is a keyword
      const onda_keyword_t* hit_keyword =
          bsearch(&k,
                  keywords,
                  sizeof(keywords) / sizeof(keywords[0]),
                  sizeof(keywords[0]),
                  cmp_key_kw);

      if (hit_keyword) {
        code[pc++] = hit_keyword->opcode;

        // Jumps need special handling, as they have a target label
        if (hit_keyword->opcode == ONDA_OP_JUMP ||
            hit_keyword->opcode == ONDA_OP_JUMP_IF) {
          // TODO: handle jump to targets
          // next token should be a label
          // if label exists, write its address
          // else, record a fixup to be resolved later
          onda_token_next(&lexer, &tok);
          if (tok.type != TOKEN_IDENTIFIER) {
            fprintf(stderr,
                    "Expected label after jump at line %lu, column %lu\n",
                    lexer.line,
                    lexer.column);
            rc = -1;
            goto done;
          }

          // Check if the label is already defined
          uint32_t bcode_pos = 0;
          if (onda_dict_get(&lexer.labels, tok.start, tok.len, &bcode_pos)) {
            // Store unresolved jump
            onda_unresolved_jump_t* uj =
                (onda_unresolved_jump_t*)onda_calloc(1, sizeof(*uj));
            uj->pc_pos = pc;
            uj->label = (char*)onda_calloc(1, (size_t)tok.len + 1);
            memcpy(uj->label, tok.start, (size_t)tok.len);
            uj->label[tok.len] = '\0';
            uj->next = unresolved_jumps;
            unresolved_jumps = uj;
          }

          // Write jump offset, or placeholder if unresolved
          const int16_t offset = (int16_t)(bcode_pos - pc);
          memcpy(&code[pc], &offset, sizeof(int16_t));
          pc += sizeof(int16_t);
        }
        break;
      }

      // TODO: check if it is a defined word in a dictionary
      uint32_t bcode_pos;
      if (onda_dict_get(&lexer.words, tok.start, tok.len, &bcode_pos) == 0) {
        // TODO: handle word calling
        // inlining: copy bytecode at bcode_pos and
      }
      fprintf(stderr,
              "Unknown word '%.*s' at line %lu, column %lu\n",
              (int)tok.len,
              tok.start,
              lexer.line,
              lexer.column);
      goto done;

      break;
    }
    case TOKEN_LABEL: {
      uint32_t jmp_target;
      if (onda_dict_get(&lexer.labels, tok.start, tok.len, &jmp_target) == 0) {
        fprintf(stderr,
                "Duplicate label '%.*s' at line %lu, column %lu\n",
                (int)tok.len,
                tok.start,
                lexer.line,
                lexer.column);
        goto done;
      }
      onda_dict_put(&lexer.labels, tok.start, tok.len, (uint32_t)pc);
      // Resolve any pending jumps to this label
      onda_unresolved_jump_t* uj = unresolved_jumps;
      onda_unresolved_jump_t* prev_uj = NULL;
      while (uj) {
        if (strncmp(uj->label, tok.start, tok.len) == 0) {
          // Patch jump offset
          int16_t offset = (int16_t)(pc - uj->pc_pos);
          memcpy(&code[uj->pc_pos], &offset, sizeof(int16_t));
          // Remove from unresolved jumps list
          if (prev_uj)
            prev_uj->next = uj->next;
          else
            unresolved_jumps = uj->next;
          onda_free(uj->label);
          onda_unresolved_jump_t* to_free = uj;
          uj = uj->next;
          onda_free(to_free);
          continue;
        }
        uj = uj->next;
      }
      break;
    }
    default:
      fprintf(stderr,
              "Unexpected token at line %lu, column %lu\n",
              lexer.line,
              lexer.column);
      goto done;
    }
  }

done:

  // Check for any remaining unresolved jumps
  while (unresolved_jumps) {
    fprintf(stderr, "Unresolved jump to label '%s'\n", unresolved_jumps->label);
    onda_free(unresolved_jumps->label);
    onda_unresolved_jump_t* to_free = unresolved_jumps;
    unresolved_jumps = unresolved_jumps->next;
    onda_free(to_free);
    rc = -1;
  }

  *code_size = pc;
  *entry_pc = 0; // Entry point at start

  onda_dict_free(&lexer.labels);
  onda_dict_free(&lexer.words);
  return rc;
}
