# KOS_OP_PUSH_FOREACH
- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`).
- **Stack**: Pops iterable (TOS); creates iterator; pushes the FOREACH node, which owns its segment. Region loop result starts unset. ITER_NEXT uses the node's iterator. Break target = `extent.pop_pc + 1` (instruction after the matching POP_FOREACH).
- **Pushes:** KOS_DYN_FOREACH (static extent KOS_EXT_FOREACH; node holds iterator and loop state).
- **Invariants**: A KOS_EXT_FOREACH extent must exist at the PUSH_FOREACH PC; otherwise RuntimeError. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_FOREACH)` → `ext->pop_pc + 1`.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., iterable]. Pool: 0 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0   | 0        | pop iterable; push FOREACH region; break_pc = pop_pc+1 from extent table |
| 2     | ITER_NEXT    | 6   | 6 (-> 6) | next from region; else → 6. stack: [..., x] |
| 3     | POP          | 1   | 1        | body: consume x |
| 4     | JUMP         | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH  | 0   | 0        | push loop result (None if unset) |
| 6     | LABEL        | --   | --        | break / exhausted (= pop_pc+1) |
