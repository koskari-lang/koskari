# KOS_OP_ITER_NEXT
- **Style**: LABEL. Arg = else branch pc (post-compile).
- **Status**: Implemented.
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: push value; else jump to `arg`. Auto-await if iterator is async.
- **Invariants**: Must be inside a PUSH_FOREACH extent. Jump target `arg` must be local.
- **Compiler form:** Pre-compile arg = string label (else / exhausted target); post-compile arg = pc.

**Example:** For-each body: get next from region iterator; if exhausted jump to 6.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable; break target from extent table |
| 2     | ITER_NEXT| 6   | 6 (-> 6)  | next from region; if exhausted → pc=6. stack: [..., val] or jump |
| 3     | POP      | 1   | 1        | consume value (body) |
| 4     | JUMP     | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 6     | LABEL    | —   | —        | else (exhausted) |
