#include "onda_parser.h"

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

void onda_token_next(onda_lexer_t* lexer, onda_token_t* out_token) {
  out_token->start = lexer->src + lexer->pos;
  out_token->len = 1;
  if (at_end(lexer)) {
    out_token->type = TOKEN_EOF;
    return;
  }

  const char c = curr(lexer);
  switch (c) {
  case ':':
    out_token->type = TOKEN_COLON;
    advance(lexer);
    return;
  case ';':
    out_token->type = TOKEN_SEMICOLON;
    advance(lexer);
    return;
  case '"': {
    int rc = lex_string(lexer, &out_token->start, &out_token->len);
    if (rc != 0) {
      out_token->type = TOKEN_INVALID;
      out_token->start = lexer->src + lexer->pos; // update start to current pos
      out_token->len = 0;
      return;
    }
    out_token->type = TOKEN_STRING;
    return;
  }
  case '+':
    out_token->type = TOKEN_OPERATOR;
    out_token->subtype = OPERATOR_ADD;
    advance(lexer);
    return;
  case '-':
    // Check if it is a unary minus from a number or
    // a minus operator
    if (!isdigit((unsigned char)nextc(lexer))) {
      out_token->type = TOKEN_OPERATOR;
      out_token->subtype = OPERATOR_SUBTRACT;
      advance(lexer);
      return;
    }
    break;
  case '*':
    out_token->type = TOKEN_OPERATOR;
    out_token->subtype = OPERATOR_MULTIPLY;
    advance(lexer);
    return;
  case '/':
    out_token->type = TOKEN_OPERATOR;
    out_token->subtype = OPERATOR_DIVIDE;
    advance(lexer);
    return;
  case '%':
    out_token->type = TOKEN_OPERATOR;
    out_token->subtype = OPERATOR_MODULO;
    advance(lexer);
    return;
  case '!':
    out_token->type = TOKEN_OPERATOR; // ! and !=
    out_token->subtype = OPERATOR_NOT;
    if (nextc(lexer) == '=') {
      out_token->len = 2;
      out_token->subtype = OPERATOR_NOT_EQUAL;
      advance(lexer);
    }
    advance(lexer);
    return;
  case '=':
    out_token->type = TOKEN_OPERATOR; // ==
    if (nextc(lexer) == '=') {
      out_token->len = 2;
      out_token->subtype = OPERATOR_EQUAL; // == operator
      advance(lexer);
    }
    advance(lexer);
    return;
  case '<':
    out_token->type = TOKEN_OPERATOR; // < and <=
    out_token->subtype = OPERATOR_LESS;
    if (nextc(lexer) == '=') {
      out_token->len = 2;
      out_token->subtype = OPERATOR_LESS_EQUAL; // <= operator
      advance(lexer);
    }
    advance(lexer);
    return;
  case '>':
    out_token->type = TOKEN_OPERATOR; // > and >=
    out_token->subtype = OPERATOR_GREATER;
    if (nextc(lexer) == '=') {
      out_token->len = 2;
      out_token->subtype = OPERATOR_GREATER_EQUAL; // >= operator
      advance(lexer);
    }
    advance(lexer);
    return;
  }

  if (isdigit(c) || (c == '-' && isdigit(nextc(lexer)))) {
    out_token->type = TOKEN_NUMBER;
    out_token->len = lex_number(lexer);
    out_token->number = parse_number(out_token->start, out_token->len);
    return;
  }

  // Parse identifier/word
  out_token->type = TOKEN_WORD;
  size_t start_pos = lexer->pos;
  // until first whitespace
  while (!at_end(lexer) && !isspace((unsigned char)curr(lexer)))
    advance(lexer);
  out_token->len = (int)(lexer->pos - start_pos);
  return;
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

void onda_parse(const char* source,
                uint8_t* code,
                size_t* code_size,
                size_t* entry_pc) {
  onda_lexer_t lexer = {
      .src = source,
      .pos = 0,
      .line = 1,
      .column = 0,
  };

  static const uint8_t oper_to_code_map[] = {
      [OPERATOR_ADD] = ONDA_OP_ADD,
      [OPERATOR_SUBTRACT] = ONDA_OP_SUB,
      [OPERATOR_MULTIPLY] = ONDA_OP_MUL,
      [OPERATOR_DIVIDE] = ONDA_OP_DIV,
      [OPERATOR_MODULO] = ONDA_OP_MOD,
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
      {"halt", ONDA_OP_HALT},
      {"or", ONDA_OP_OR},
      {"over", ONDA_OP_OVER},
      {"rot", ONDA_OP_ROT},
      {"swap", ONDA_OP_SWAP},
  };

  size_t pc = 0;
  while (true) {
    skip_whitespace_and_comments(&lexer);
    onda_token_t tok;
    onda_token_next(&lexer, &tok);
    if (tok.type == TOKEN_EOF) {
      break;
    } else if (tok.type == TOKEN_INVALID) {
      fprintf(stderr,
              "Lexer error at line %lu, column %lu\n",
              lexer.line,
              lexer.column);
      return;
    }

    switch (tok.type) {
    case TOKEN_NUMBER:
      if (tok.number <= 0x7F && tok.number >= -0x80) {
        if (pc + 2 > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          return;
        }
        code[pc++] = ONDA_OP_PUSH_CONST_U8;
        code[pc++] = (int8_t)tok.number;
      } else if (tok.number <= 0x7FFFFFFF && tok.number >= -0x80000000) {
        if (pc + 1 + sizeof(int32_t) > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          return;
        }
        code[pc++] = ONDA_OP_PUSH_CONST_U32;
        uint32_t val = (int32_t)(tok.number);
        memcpy(&code[pc], &val, sizeof(uint32_t));
        pc += sizeof(int32_t);
      } else {
        if (pc + 1 + sizeof(int64_t) > *code_size) {
          fprintf(stderr, "Code buffer overflow\n");
          return;
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
        return;
      }
      code[pc++] = oper_to_code_map[tok.subtype];
      break;
    case TOKEN_STRING: {
      char* str_data = onda_malloc(tok.len + 1);
      memcpy(str_data, tok.start, tok.len);
      str_data[tok.len] = '\0';
      code[pc++] = ONDA_OP_PUSH_CONST_U64;
      uint64_t addr = (uint64_t)(uintptr_t)str_data;
      memcpy(&code[pc], &addr, sizeof(uint64_t));
      pc += sizeof(uint64_t);
      break;
    }
    case TOKEN_WORD: {
      if (pc + 1 > *code_size) {
        fprintf(stderr, "Code buffer overflow\n");
        return;
      }
      str_slice_t k = {.s = tok.start, .len = tok.len};

      // Check if it is a keyword
      const onda_keyword_t* hit =
          bsearch(&k,
                  keywords,
                  sizeof(keywords) / sizeof(keywords[0]),
                  sizeof(keywords[0]),
                  cmp_key_kw);

      if (hit) {
        code[pc++] = hit->opcode;
        break;
      }

      // TODO: check if it is a defined word in a dictionary
      fprintf(stderr,
              "Unknown word '%.*s' at line %lu, column %lu\n",
              (int)tok.len,
              tok.start,
              lexer.line,
              lexer.column);
      return;

      break;
    }
    default:
      fprintf(stderr,
              "Unexpected token at line %lu, column %lu\n",
              lexer.line,
              lexer.column);
      return;
    }
  }

  *code_size = pc;
  *entry_pc = 0; // Entry point at start
}
