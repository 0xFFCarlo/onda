- jmp and jmp_if instructions should support labels as
  targets.
- test jmp, jmp_if, labels in vm
- test jmp, jmp_if, labels in aarch64 compiler
- store constants/data (eg strings) in a data section (no
  random allocations) so that bytecode can be saved to file
  with constants too.
- words definition
- call words / inline words
- call c functions:
  - last argument is number of arguments
  - vm: libffi
  - aarch64: pass args in x0-x7


add labels in dictionary as I go.
If goto can't find match, add to a list of unresolved gotos.
When a label is added, resolve any gotos that were waiting for it.
Labels ids should have scope bits so that nested labels can be supported.
No need to have a label instruction, inlined words are added
to the program on the fly before evaluating next labels.
store also labels pc

After all goto are resolved, turn all label ids into offsets
to label.

goto offset -> 
goto_if -> 


(cond) if ..A.. endif
    eval cond
    if -> JMP_IF_FALSE 0
    ... A ...
    endif -> LABEL 0

(cond) if ..A.. else ..B.. endif
    eval cond
    if -> JMP_IF_FALSE 0
    ... A ...
    else -> JMP 1
    else -> LABEL 0
    ... B ...
    LABEL 1

while (cond) do { body } endwhile
    while -> LABEL 0
    eval cond
    do-> JMP_IF_FALSE 1
    ... body ...
    endwhile -> JMP 0
    endwhile -> LABEL 1
