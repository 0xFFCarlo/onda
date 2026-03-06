#include "onda_jit.h"
#if defined(__aarch64__)
#include "onda_jit_aarch64.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#endif

typedef uint64_t (*jit_fn)(void);

uint64_t onda_jit_run(const uint8_t* machine_code, size_t machine_code_size) {
  long pagesz_l = sysconf(_SC_PAGESIZE);
  size_t pagesz = (pagesz_l > 0) ? (size_t)pagesz_l : 4096;
  size_t allocsz = (machine_code_size + pagesz - 1) & ~(pagesz - 1);

#if defined(__APPLE__)
  void* mem = mmap(NULL,
                   allocsz,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_JIT,
                   -1,
                   0);
#else
  void* mem = mmap(NULL,
                   allocsz,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS,
                   -1,
                   0);
#endif
  if (mem == MAP_FAILED) {
    fprintf(stderr, "mmap failed: %s\n", strerror(errno));
    return 1;
  }

#if defined(__APPLE__)
  pthread_jit_write_protect_np(0);
#endif

  memcpy(mem, machine_code, machine_code_size);

#if defined(__APPLE__)
  sys_icache_invalidate(mem, machine_code_size);
#else
  __builtin___clear_cache((char*)mem, (char*)mem + machine_code_size);
#endif

#if defined(__APPLE__)
  pthread_jit_write_protect_np(1);
#endif
  if (mprotect(mem, allocsz, PROT_READ | PROT_EXEC) != 0) {
    fprintf(stderr, "mprotect failed: %s\n", strerror(errno));
    munmap(mem, allocsz);
    return 1;
  }

  jit_fn fn = (jit_fn)mem;
  uint64_t ret = fn();

  munmap(mem, allocsz);
  return ret;
}

int onda_jit_compile(const uint8_t* bytecode,
                     const size_t bytecode_entry_pc,
                     size_t bytecode_size,
                     int64_t* frame_bp,
                     uint8_t** out_machine_code,
                     size_t* out_machine_code_size) {
#if defined(__aarch64__)
  return onda_jit_aarch64(bytecode,
                          bytecode_entry_pc,
                          bytecode_size,
                          frame_bp,
                          out_machine_code,
                          out_machine_code_size);
#else
  return -1; // JIT compilation not supported on this platform
#endif // __aarch64__
}
