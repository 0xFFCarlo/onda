#include "onda_compiler.h"
#include "onda_config.h"
#include "onda_jit.h"
#include "onda_std.h"
#include "onda_vm.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ONDA_BC_VERSION 1u

enum {
  ONDA_EXIT_OK = 0,
  ONDA_EXIT_USAGE = 2,
  ONDA_EXIT_COMPILE = 3,
  ONDA_EXIT_LOAD = 4,
  ONDA_EXIT_RUNTIME = 5,
};

typedef struct {
  char magic[4];
  uint32_t version;
  uint64_t entry_pc;
  uint64_t code_size;
  uint64_t const_pool_size;
} onda_bc_header_t;

typedef struct {
  const uint8_t* code;
  size_t code_size;
  size_t entry_pc;
  const uint8_t* const_pool;
  size_t const_pool_size;
} onda_program_t;

typedef struct {
  bool no_jit;
  bool show_time;
  bool print_bytecode;
  const char* path;
  const char* inline_source;
} run_opts_t;

typedef struct {
  bool show_time;
  bool print_bytecode;
  const char* src_path;
  const char* out_path;
} build_opts_t;

static double elapsed_ms(const struct timespec* start,
                         const struct timespec* end) {
  return (end->tv_sec - start->tv_sec) * 1000.0 +
         (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

static onda_program_t program_from_cobj(const onda_code_obj_t* cobj) {
  const onda_program_t prog = {
      .code = cobj->code,
      .code_size = cobj->size,
      .entry_pc = cobj->entry_pc,
      .const_pool = cobj->const_pool,
      .const_pool_size = cobj->const_pool_size,
  };
  return prog;
}

static void usage(const char* prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s run [--no-jit] [--time] [--print-bytecode] "
          "(-e <source_string> | <source_file>)\n"
          "  %s build [--time] [--print-bytecode] <source_file> "
          "<output_bytecode>\n"
          "  %s exec [--no-jit] [--time] [--print-bytecode] <bytecode_file>\n",
          prog,
          prog,
          prog);
}

static int parse_run_opts(int argc,
                          char* argv[],
                          int start,
                          bool allow_inline,
                          run_opts_t* out) {
  int file_count = 0;
  int inline_count = 0;
  *out = (run_opts_t){0};

  for (int i = start; i < argc; i++) {
    if (strcmp(argv[i], "--no-jit") == 0) {
      out->no_jit = true;
    } else if (strcmp(argv[i], "--time") == 0) {
      out->show_time = true;
    } else if (strcmp(argv[i], "--print-bytecode") == 0) {
      out->print_bytecode = true;
    } else if (strcmp(argv[i], "-e") == 0) {
      if (!allow_inline || i + 1 >= argc || inline_count > 0)
        return -1;
      out->inline_source = argv[++i];
      inline_count++;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown flag: %s\n", argv[i]);
      return -1;
    } else {
      out->path = argv[i];
      file_count++;
    }
  }

  return (file_count + inline_count) == 1 ? 0 : -1;
}

static int parse_build_opts(int argc, char* argv[], build_opts_t* out) {
  *out = (build_opts_t){0};

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--time") == 0) {
      out->show_time = true;
    } else if (strcmp(argv[i], "--print-bytecode") == 0) {
      out->print_bytecode = true;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown flag: %s\n", argv[i]);
      return -1;
    } else if (!out->src_path) {
      out->src_path = argv[i];
    } else if (!out->out_path) {
      out->out_path = argv[i];
    } else {
      return -1;
    }
  }

  return (out->src_path && out->out_path) ? 0 : -1;
}

static int write_bytecode_file(const char* out_path,
                               const onda_program_t* prog) {
  FILE* f = fopen(out_path, "wb");
  if (!f) {
    fprintf(stderr,
            "Failed to open output file '%s': %s\n",
            out_path,
            strerror(errno));
    return -1;
  }

  const onda_bc_header_t hdr = {
      .magic = {'O', 'N', 'B', 'C'},
      .version = ONDA_BC_VERSION,
      .entry_pc = (uint64_t)prog->entry_pc,
      .code_size = (uint64_t)prog->code_size,
      .const_pool_size = (uint64_t)prog->const_pool_size,
  };

  if (fwrite(&hdr, sizeof(hdr), 1, f) != 1 ||
      fwrite(prog->code, 1, prog->code_size, f) != prog->code_size ||
      fwrite(prog->const_pool, 1, prog->const_pool_size, f) !=
          prog->const_pool_size) {
    fprintf(stderr, "Failed to write bytecode file '%s'\n", out_path);
    fclose(f);
    return -1;
  }

  fclose(f);
  return 0;
}

static int read_bytecode_file(const char* in_path,
                              uint8_t** out_code,
                              size_t* out_code_size,
                              size_t* out_entry_pc,
                              uint8_t** out_const_pool,
                              size_t* out_const_pool_size) {
  FILE* f = fopen(in_path, "rb");
  if (!f) {
    fprintf(stderr,
            "Failed to open bytecode file '%s': %s\n",
            in_path,
            strerror(errno));
    return -1;
  }

  onda_bc_header_t hdr;
  if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
    fprintf(stderr, "Failed to read bytecode header from '%s'\n", in_path);
    fclose(f);
    return -1;
  }

  if (memcmp(hdr.magic, "ONBC", 4) != 0 || hdr.version != ONDA_BC_VERSION) {
    fprintf(stderr, "Invalid bytecode format: %s\n", in_path);
    fclose(f);
    return -1;
  }

  if (hdr.code_size > SIZE_MAX || hdr.const_pool_size > SIZE_MAX ||
      hdr.entry_pc > SIZE_MAX) {
    fprintf(stderr, "Bytecode file too large: %s\n", in_path);
    fclose(f);
    return -1;
  }

  uint8_t* code = malloc((size_t)hdr.code_size);
  if (!code && hdr.code_size > 0) {
    fprintf(stderr, "Out of memory loading bytecode\n");
    fclose(f);
    return -1;
  }
  if (fread(code, 1, (size_t)hdr.code_size, f) != (size_t)hdr.code_size) {
    fprintf(stderr, "Failed to read code section from '%s'\n", in_path);
    fclose(f);
    free(code);
    return -1;
  }

  uint8_t* const_pool = NULL;
  if (hdr.const_pool_size > 0) {
    const_pool = malloc((size_t)hdr.const_pool_size);
    if (!const_pool) {
      fprintf(stderr, "Out of memory loading constant pool\n");
      fclose(f);
      free(code);
      return -1;
    }
    if (fread(const_pool, 1, (size_t)hdr.const_pool_size, f) !=
        (size_t)hdr.const_pool_size) {
      fprintf(stderr, "Failed to read constant pool from '%s'\n", in_path);
      fclose(f);
      free(code);
      free(const_pool);
      return -1;
    }
  }

  fclose(f);
  *out_code = code;
  *out_code_size = (size_t)hdr.code_size;
  *out_entry_pc = (size_t)hdr.entry_pc;
  *out_const_pool = const_pool;
  *out_const_pool_size = (size_t)hdr.const_pool_size;
  return 0;
}

static int compile_program(const char* path,
                           const char* inline_source,
                           onda_lexer_t* lexer,
                           onda_env_t* env,
                           onda_code_obj_t* out_cobj) {
  if (inline_source) {
    *lexer = (onda_lexer_t){
        .src = inline_source,
        .filename = "<arg>",
    };
    if (onda_compile(lexer, env, out_cobj) != 0) {
      fprintf(stderr, "Failed to parse source from -e argument\n");
      return -1;
    }
    return 0;
  }

  if (onda_compile_file(path, lexer, env, out_cobj) != 0) {
    fprintf(stderr, "Failed to parse source file: %s\n", path);
    return -1;
  }
  return 0;
}

static int run_program(const onda_program_t* prog,
                       onda_env_t* env,
                       bool no_jit,
                       bool show_time,
                       double* out_exec_ms) {
  struct timespec start, end;
  if (out_exec_ms)
    *out_exec_ms = 0.0;

#ifdef ONDA_CAN_JIT
  if (!no_jit) {
    uint8_t* machine_code = NULL;
    size_t machine_code_size = 0;
    onda_runtime_t rt = {
        .code = prog->code,
        .code_size = prog->code_size,
        .entry_pc = prog->entry_pc,
        .const_pool = prog->const_pool,
        .const_pool_size = prog->const_pool_size,
        .native_registry = &env->native_registry,
    };
    onda_runtime_reset(&rt);

    if (onda_jit_compile(&rt, &machine_code, &machine_code_size) == 0) {
      if (show_time)
        clock_gettime(CLOCK_MONOTONIC, &start);
      (void)onda_jit_run(machine_code, machine_code_size);
      if (show_time)
        clock_gettime(CLOCK_MONOTONIC, &end);
      if (show_time && out_exec_ms)
        *out_exec_ms = elapsed_ms(&start, &end);
      free(machine_code);
      return ONDA_EXIT_OK;
    }

    free(machine_code);
  }
#else
  (void)no_jit;
#endif

  onda_vm_t* vm = onda_vm_new();
  if (!vm) {
    fprintf(stderr, "Failed to initialize VM\n");
    return ONDA_EXIT_RUNTIME;
  }

  onda_vm_load_code(vm,
                    prog->code,
                    prog->entry_pc,
                    prog->code_size,
                    prog->const_pool,
                    prog->const_pool_size);
  vm->env = env;
  vm->debug_mode = false;

  if (show_time)
    clock_gettime(CLOCK_MONOTONIC, &start);
  if (onda_vm_run(vm) != 0) {
    onda_vm_free(vm);
    return ONDA_EXIT_RUNTIME;
  }
  if (show_time)
    clock_gettime(CLOCK_MONOTONIC, &end);
  if (show_time && out_exec_ms)
    *out_exec_ms = elapsed_ms(&start, &end);

  onda_vm_free(vm);
  return ONDA_EXIT_OK;
}

static int print_and_run(const onda_program_t* prog,
                         onda_env_t* env,
                         bool print_bytecode,
                         bool no_jit,
                         bool show_time,
                         double* out_exec_ms) {
  if (print_bytecode)
    onda_vm_print_bytecode(prog->code, prog->code_size);
  return run_program(prog, env, no_jit, show_time, out_exec_ms);
}

static int cmd_build(int argc,
                     char* argv[],
                     onda_lexer_t* lexer,
                     onda_env_t* env) {
  build_opts_t opts;
  onda_code_obj_t cobj = {0};
  struct timespec compile_start, compile_end;

  if (parse_build_opts(argc, argv, &opts) != 0)
    return ONDA_EXIT_USAGE;

  onda_code_obj_init(&cobj, ONDA_CODE_BUF_SIZE);
  if (opts.show_time)
    clock_gettime(CLOCK_MONOTONIC, &compile_start);
  if (compile_program(opts.src_path, NULL, lexer, env, &cobj) != 0) {
    onda_code_obj_free(&cobj);
    return ONDA_EXIT_COMPILE;
  }
  if (opts.show_time)
    clock_gettime(CLOCK_MONOTONIC, &compile_end);

  const onda_program_t prog = program_from_cobj(&cobj);
  if (opts.print_bytecode)
    onda_vm_print_bytecode(prog.code, prog.code_size);

  int rc = ONDA_EXIT_OK;
  if (write_bytecode_file(opts.out_path, &prog) != 0) {
    rc = ONDA_EXIT_LOAD;
  } else {
    printf("Wrote bytecode: %s\n", opts.out_path);
    if (opts.show_time)
      printf("Compilation time: %.3f ms\n",
             elapsed_ms(&compile_start, &compile_end));
  }

  onda_code_obj_free(&cobj);
  return rc;
}

static int cmd_run_or_exec(const char* cmd,
                           int argc,
                           char* argv[],
                           onda_lexer_t* lexer,
                           onda_env_t* env) {
  run_opts_t opts;
  double exec_ms = 0.0;
  if (parse_run_opts(argc, argv, 2, strcmp(cmd, "run") == 0, &opts) != 0)
    return ONDA_EXIT_USAGE;

  if (strcmp(cmd, "exec") == 0) {
    uint8_t* code = NULL;
    uint8_t* const_pool = NULL;
    size_t code_size = 0;
    size_t entry_pc = 0;
    size_t const_pool_size = 0;
    int rc = ONDA_EXIT_OK;

    if (read_bytecode_file(opts.path,
                           &code,
                           &code_size,
                           &entry_pc,
                           &const_pool,
                           &const_pool_size) != 0) {
      rc = ONDA_EXIT_LOAD;
    } else {
      const onda_program_t prog = {
          .code = code,
          .code_size = code_size,
          .entry_pc = entry_pc,
          .const_pool = const_pool,
          .const_pool_size = const_pool_size,
      };
      rc = print_and_run(&prog,
                         env,
                         opts.print_bytecode,
                         opts.no_jit,
                         opts.show_time,
                         &exec_ms);
    }

    free(code);
    free(const_pool);
    if (rc == ONDA_EXIT_OK && opts.show_time)
      printf("Execution time: %.3f ms\n", exec_ms);
    return rc;
  }

  onda_code_obj_t cobj = {0};
  struct timespec compile_start, compile_end;
  onda_code_obj_init(&cobj, ONDA_CODE_BUF_SIZE);

  if (opts.show_time)
    clock_gettime(CLOCK_MONOTONIC, &compile_start);
  if (compile_program(opts.path, opts.inline_source, lexer, env, &cobj) != 0) {
    onda_code_obj_free(&cobj);
    return ONDA_EXIT_COMPILE;
  }
  if (opts.show_time)
    clock_gettime(CLOCK_MONOTONIC, &compile_end);

  const onda_program_t prog = program_from_cobj(&cobj);
  const int rc = print_and_run(&prog,
                               env,
                               opts.print_bytecode,
                               opts.no_jit,
                               opts.show_time,
                               &exec_ms);

  onda_code_obj_free(&cobj);
  if (rc == ONDA_EXIT_OK && opts.show_time) {
    printf("Compilation time: %.3f ms\n",
           elapsed_ms(&compile_start, &compile_end));
    printf("Execution time: %.3f ms\n", exec_ms);
  }
  return rc;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return ONDA_EXIT_USAGE;
  }

  const char* cmd = argv[1];
  if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 ||
      strcmp(cmd, "help") == 0) {
    usage(argv[0]);
    return ONDA_EXIT_OK;
  }

  onda_lexer_t lexer = {0};
  onda_env_t env;
  onda_env_init(&env);
  onda_env_register_std(&env);

  int rc;
  if (strcmp(cmd, "build") == 0) {
    rc = cmd_build(argc, argv, &lexer, &env);
  } else if (strcmp(cmd, "run") == 0 || strcmp(cmd, "exec") == 0) {
    rc = cmd_run_or_exec(cmd, argc, argv, &lexer, &env);
  } else if (argc == 2) {
    char run_cmd[] = "run";
    char* compat_argv[] = {argv[0], run_cmd, argv[1]};
    rc = cmd_run_or_exec("run", 3, compat_argv, &lexer, &env);
  } else {
    fprintf(stderr, "Unknown command: %s\n", cmd);
    rc = ONDA_EXIT_USAGE;
  }

  if (rc == ONDA_EXIT_USAGE)
    usage(argv[0]);

  onda_env_free(&env);
  return rc;
}
