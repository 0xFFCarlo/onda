- call c functions:
    - function callback manipulates data stack
    - do not use sp in jit use separate buffer for data stack
      in this way calling c functions callbacks in jit code 
      is much easier.
- alias words (inlined)
- store constants/data (eg strings) in a data section (no
  random allocations) so that bytecode can be saved to file
  with constants too.
