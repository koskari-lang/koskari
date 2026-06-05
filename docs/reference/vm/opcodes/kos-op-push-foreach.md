# KOS_OP_PUSH_FOREACH
- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`).
- **Status**: Implemented.
- **Stack**: Pops iterable (TOS) and loop_result (below TOS); creates iterator; pushes FOREACH node and stack block. ITER_NEXT uses the node's iterator. Break target = `extent.pop_pc + 1` (instruction after the matching POP_FOREACH).
- **Pushes:** KOS_DYN_FOREACH (static extent KOS_EXT_FOREACH; node holds iterator and loop state).
- **Invariants**: A KOS_EXT_FOREACH extent must exist at the PUSH_FOREACH PC; otherwise RuntimeError. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_FOREACH)` → `ext->pop_pc + 1`.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., loop_result, iterable]. Pool: 0 = None, 1 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (None) | stack: [..., loop_result] |
| 1     | CONST        | 1   | 1 ([1,2,3]) | stack: [..., loop_result, iterable] |
| 2     | PUSH_FOREACH | 0   | 0        | pop both; push FOREACH region; break_pc = pop_pc+1 from extent table |
| 3     | ITER_NEXT    | 7   | 7 (-> 7) | next from region; else → 7. stack: [..., x] |
| 4     | POP          | 1   | 1        | body: consume x |
| 5     | JUMP         | 3   | 3 (-> 3) | loop back |
| 6     | POP_FOREACH  | 0   | 0        | |
| 7     | LABEL        | —   | —        | break / exhausted (= pop_pc+1) |

