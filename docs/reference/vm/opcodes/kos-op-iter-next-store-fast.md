# KOS_OP_ITER_NEXT_STORE_FAST
- **Style**: EXTRA. Fields: FASTVAR (slot), LABEL (else branch pc, post-compile).
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: store the element into the fast slot, stack unchanged; else jump to the label. Auto-await if the iterator is async: a deferred element is stored into the slot when it arrives.
- **Invariants**: Must be inside a PUSH_FOREACH extent. The label must be local. The slot must be a fast slot of the current frame namespace.
- **Compiler form:** Produced by the processor from `ITER_NEXT` followed by `VAR_STORE`, as `ITER_NEXT_STORE_VAR`, then lowered to this opcode when the name has a fast slot. The for-each special's binding form compiles to it.

**Example:** For-each body: next from the region iterator into slot 0; if exhausted jump to 5.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable |
| 2     | ITER_NEXT_STORE_FAST | 0 | (0, -> 5) | next from region into slot 0; if exhausted -> pc=5. stack unchanged |
| 3     | CONTINUE | 0   | 0        | back to the step |
| 4     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 5     | LABEL    | --   | --        | else (exhausted) |
