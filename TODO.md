- continue keyword in while loop
- Byte-addressed memory ops: c@ c! (8-bit), w@ w! (16-bit), @ ! (64-bit)
- build in alloc / free functions
- alias words (inlined)
- locals defined in word header after arguments
- store constants/data (eg strings) in a data section (no
  random allocations) so that bytecode can be saved to file
  with constants too.
- call c functions:
  - last argument is number of arguments
  - vm: libffi
  - aarch64: pass args in x0-x7
