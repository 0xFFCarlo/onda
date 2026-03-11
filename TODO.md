- native function call should not store in bytecode the address otherwise
  it does not work across compilation to bytecode and running. We should
  store ids of the native function table instead and resolve them at runtime.
- alias words (inlined)
- store constants/data (eg strings) in a data section (no
  random allocations) so that bytecode can be saved and loaded without issues. 
  Maybe it make sense to have a PUSH CONST opcode to push a ptr relative the
  const pool or push a value in a position of the const pool.
