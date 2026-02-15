#ifndef ONDA_COMPILER_H
#define ONDA_COMPILER_H

#include "onda_dict.h"

#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  TOKEN_COLON,      // :
  TOKEN_SEMICOLON,  // ;
  TOKEN_NUMBER,     // 42, -3.14
  TOKEN_STRING,     // "Mario"
  TOKEN_IDENTIFIER, // identifier
  TOKEN_INVALID,    // invalid token
  TOKEN_EOF,        // end of file
} onda_token_type;

typedef struct {
  onda_token_type type;
  union {
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

typedef struct {
  const char* src;
  size_t pos;
  size_t line;
  size_t column;
  onda_dict_t words;
} onda_lexer_t;

typedef struct {
  uint8_t* code;
  size_t size;
  size_t capacity;
  size_t entry_pc;
} onda_code_obj_t;

// Get next token from the lexer
void onda_token_next(onda_lexer_t* lexer, onda_token_t* out_token);

// Parse source code into bytecode buffer and set entry point if present
int onda_compile(onda_lexer_t* lexer, onda_code_obj_t* code_obj);

#endif // ONDA_COMPILER_H
