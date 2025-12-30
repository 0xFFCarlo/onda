#ifndef ONDA_PARSER_H
#define ONDA_PARSER_H

#include "onda_dict.h"

#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  TOKEN_COLON,      // :
  TOKEN_SEMICOLON,  // ;
  TOKEN_NUMBER,     // 42, -3.14
  TOKEN_STRING,     // "Mario"
  TOKEN_OPERATOR,   // + - * / ! % = == != < > <= >= & |
  TOKEN_LABEL,      // @label
  TOKEN_IDENTIFIER, // identifier
  TOKEN_INVALID,    // invalid token
  TOKEN_EOF,        // end of file
} onda_token_type;

typedef enum : uint8_t {
  OPERATOR_ADD,           // +
  OPERATOR_SUBTRACT,      // -
  OPERATOR_MULTIPLY,      // *
  OPERATOR_DIVIDE,        // /
  OPERATOR_INC,           // ++
  OPERATOR_DEC,           // --
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
  const char* start;
  int len;
} onda_token_t;

// Used to track word locations in the bytecode
typedef struct onda_word_info_t {
  char* name;
  size_t name_len;
  size_t pc;
  size_t word_len;
} onda_word_info_t;


// Used to track unresolved jumps to labels during parsing
typedef struct onda_unresolved_jump_t {
  size_t pc_pos;
  char* label;
  struct onda_unresolved_jump_t* next;
} onda_unresolved_jump_t;

typedef struct {
  const char* src;
  size_t pos;
  size_t line;
  size_t column;
  onda_dict_t words;
  onda_dict_t labels;
  size_t* entry_pc;
  onda_unresolved_jump_t* unresolved_jumps;
  onda_word_info_t current_word;
} onda_lexer_t;

// Get next token from the lexer
void onda_token_next(onda_lexer_t* lexer, onda_token_t* out_token);

// Parse source code into bytecode buffer and set entry point if present
int onda_parse(const char* source,
               uint8_t* code,
               size_t code_buf_size,
               size_t* out_code_size,
               size_t* entry_pc);

// Parse bytecode from file
int onda_parse_file(const char* filename,
                    uint8_t* code,
                    size_t code_buf_size,
                    size_t* out_code_size,
                    size_t* entry_pc);

#endif // ONDA_LEXER_H
