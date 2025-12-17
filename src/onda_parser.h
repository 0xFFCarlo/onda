#ifndef ONDA_PARSER_H
#define ONDA_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  TOKEN_COLON,     // :
  TOKEN_SEMICOLON, // ;
  TOKEN_NUMBER,    // 42, -3.14
  TOKEN_STRING,    // "Mario"
  TOKEN_OPERATOR,  // + - * / ! % = == != < > <= >= & |
  TOKEN_WORD,      // identifier
  TOKEN_INVALID,   // invalid token
  TOKEN_EOF,       // end of file
} onda_token_type;

typedef enum : uint8_t {
  OPERATOR_ADD,           // +
  OPERATOR_SUBTRACT,      // -
  OPERATOR_MULTIPLY,      // *
  OPERATOR_DIVIDE,        // /
  OPERATOR_MODULO,        // %
  OPERATOR_NOT,           // !
  OPERATOR_EQUAL,         // ==
  OPERATOR_NOT_EQUAL,     // !=
  OPERATOR_LESS,          // <
  OPERATOR_LESS_EQUAL,    // <=
  OPERATOR_GREATER,       // >
  OPERATOR_GREATER_EQUAL, // >=
  OPERATOR_LOGICAL_AND,   // and
  OPERATOR_LOGICAL_OR,    // or
  OPERATOR_COUNT,
} onda_oper_kind;

typedef struct {
  onda_token_type type;
  union {
    uint8_t subtype;
    int64_t number;
  };
  const char *start;
  int len;
} onda_token_t;

typedef struct {
  const char *src;
  size_t pos;
  size_t line;
  size_t column;
} onda_lexer_t;

// Get next token from the lexer
void onda_token_next(onda_lexer_t *lexer, onda_token_t *out_token);

// Parse source code into bytecode buffer and set entry point if present
void onda_parse(const char *source, uint8_t *code, size_t *code_size,
                size_t *entry_pc);

#endif // ONDA_LEXER_H
