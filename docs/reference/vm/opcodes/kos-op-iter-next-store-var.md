# KOS_OP_ITER_NEXT_STORE_VAR
- **Style**: EXTRA. Fields: VAR (name), LABEL (else branch pc, post-compile).
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: store the element under the name in the current frame namespace, stack unchanged; else jump to the label. Auto-await if the iterator is async: a deferred element is stored under the name when it arrives.
- **Invariants**: Must be inside a PUSH_FOREACH extent. The label must be local.
- **Compiler form:** Produced by the processor from `ITER_NEXT` followed by `VAR_STORE`. A name with a fast slot lowers the opcode to `ITER_NEXT_STORE_FAST`; this form remains where the name has none, such as a module-level loop.

**Example:** Module-level for-each: next from the region iterator into `x`; if exhausted jump to 5.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable |
| 2     | ITER_NEXT_STORE_VAR | 0 | (x, -> 5) | next from region into x; if exhausted -> pc=5. stack unchanged |
| 3     | CONTINUE | 0   | 0        | back to the step |
| 4     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 5     | LABEL    | --   | --        | else (exhausted) |
