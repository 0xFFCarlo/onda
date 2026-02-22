#ifndef ONDA_COMPILER_H
#define ONDA_COMPILER_H

#include "onda_dict.h"
#include "onda_vm.h"

#include <stddef.h>
#include <stdint.h>

// Maximum word name lenght
#define ONDA_MAX_WORD_LEN 32

// Locals ids in bytecode start from 2
// as 0 and 1 are reserved for return address
// and previous frame base pointer
#define ONDA_LOCALS_BASE_OFF 2

typedef enum : uint8_t {
  TOKEN_COLON,      // :
  TOKEN_SEMICOLON,  // ;
  TOKEN_LPAREN,     // (
  TOKEN_RPAREN,     // )
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
typedef struct onda_word_t {
  char name[ONDA_MAX_WORD_LEN];
  size_t name_len;
  size_t pc;
  size_t word_len;
  uint8_t locals_count;
  uint8_t args_count;
} onda_word_t;

typedef struct {
  const char* src;
  size_t pos;
  size_t line;
  size_t column;
} onda_lexer_t;

typedef struct onda_scope {
  onda_dict_t locals;
  size_t locals_count;
  struct onda_scope* parent;
} onda_scope_t;

typedef struct {
  uint8_t* code;
  size_t size;
  size_t capacity;
  size_t entry_pc;
  onda_dict_t words_map;
  onda_word_t* words;
  size_t words_count;
  onda_scope_t* current_scope;
  onda_native_table_t native_funcs;
} onda_code_obj_t;

// Get next token from the lexer
void onda_token_next(onda_lexer_t* lexer, onda_token_t* out_token);

// Initialize code object with given initial capacity for bytecode buffer
int onda_code_obj_init(onda_code_obj_t* cobj, size_t initial_capacity);

// Free resources associated with code object
void onda_code_obj_free(onda_code_obj_t* cobj);

// Parse source code into bytecode buffer and set entry point if present
int onda_compile(onda_lexer_t* lexer, onda_code_obj_t* code_obj);

#endif // ONDA_COMPILER_H
