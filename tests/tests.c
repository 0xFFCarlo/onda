#include <stddef.h>
#include <stdint.h>

int tests_arithmetic() {
  const char* program = "10 6 - 10 * 2 / 3 + ret";
  uint8_t codebuf[1024];
  size_t code_size = sizeof(codebuf);
  size_t entry_pc = 0;
  onda_parse(program, codebuf, &code_size, &entry_pc);
  return 0;
}

int main() {
  tests_arithmetic();
  return 0;
}
