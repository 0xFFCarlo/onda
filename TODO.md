- make c function call work in VM
- onda_state_t stores data stack, frame stack, constants, C function table, maybe code+entry_pc
  and it is shared between VM and JIT.
- call c functions:
    - function callback manipulates data stack
    - do not use sp in jit use separate buffer for data stack
      in this way calling c functions callbacks in jit code 
      is much easier.
- remove special opcode for malloc and print, use c function call opcode
- alias words (inlined)
- store constants/data (eg strings) in a data section (no
  random allocations) so that bytecode can be saved to file
  with constants too.
