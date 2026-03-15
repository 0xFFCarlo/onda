#ifndef ONDA_COMPILER_H
#define ONDA_COMPILER_H

#include "onda_config.h"
#include "onda_dict.h"
#include "onda_env.h"
#include "onda_vm.h"

#include <stddef.h>
#include <stdint.h>

// Locals ids in bytecode start from 2 as position 0 and 1 in stack frame
// are reserved for return address and previous frame base pointer
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
  size_t len;
} onda_token_t;

typedef struct {
  const char* src;
  const char* filename;
  const char* filepath;
  const char* import_stack[ONDA_MAX_IMPORT_DEPTH];
  size_t import_depth;
  size_t pos;
  size_t line;
  size_t column;
} onda_lexer_t;

// Used to track word locations in the bytecode
typedef struct onda_word_t {
  char name[ONDA_MAX_WORD_NAME_LEN];
  size_t name_len;
  size_t pc;
  size_t word_len;      // length of the word body in bytecode
  uint8_t locals_count; // Number of locals including arguments and temporaries
  uint8_t args_count;
} onda_word_t;

typedef struct onda_alias_t {
  char name[ONDA_MAX_WORD_NAME_LEN];
  size_t name_len;
  char* body_src;
} onda_alias_t;

typedef struct onda_scope {
  onda_dict_t locals;
  size_t locals_count;
  struct onda_scope* parent;
} onda_scope_t;

typedef struct {
  // Bytecode buffer
  uint8_t* code;
  size_t size;
  size_t capacity;
  // Program entry point in bytecode, set to 0 if not defined or to main word
  size_t entry_pc;
  // Constant data pool referenced by bytecode offsets.
  uint8_t* const_pool;
  size_t const_pool_size;
  size_t const_pool_capacity;
  // Mapping of word names to their definitions
  onda_dict_t words_map;
  // Word definitions
  onda_word_t* words;
  size_t words_count;
  // Alias definitions
  onda_dict_t aliases_map;
  onda_alias_t* aliases;
  size_t aliases_count;
  // Current alias expansion depth used to avoid infinite alias recursion.
  size_t alias_expand_depth;
  // Current scope for resolving local variables. Initially NULL.
  onda_scope_t* current_scope;
  // Native functions callable from the bytecode.
  onda_native_registry_t native_funcs;
  // Tracks innermost loop start pc for handling continue statements.
  // Its equal to -1 if not in a loop.
  int32_t inner_loop_start_pc;
  // Last emitted opcode start positions for peephole optimizations.
  uint8_t recent_opcodes[3];
  size_t recent_opcode_pos[3];
  uint8_t recent_opcode_count;
} onda_code_obj_t;

// Get next token from the lexer
void onda_token_next(onda_lexer_t* lexer, onda_token_t* out_token);

// Initialize code object with given initial capacity for bytecode buffer
int onda_code_obj_init(onda_code_obj_t* cobj, size_t initial_capacity);

// Free resources associated with code object
void onda_code_obj_free(onda_code_obj_t* cobj);

// Parse source code into bytecode buffer and set entry point if present
int onda_compile(onda_lexer_t* lexer, onda_env_t* env, onda_code_obj_t* code_obj);

// Parse source code from file into bytecode buffer and set entry point
int onda_compile_file(const char* filepath,
                      onda_lexer_t* lexer,
                      onda_env_t* env,
                      onda_code_obj_t* code_obj);

#endif // ONDA_COMPILER_H
