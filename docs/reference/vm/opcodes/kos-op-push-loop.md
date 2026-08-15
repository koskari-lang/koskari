# KOS_OP_PUSH_LOOP
- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`). Continue target = instruction immediately following PUSH_LOOP (i.e. `CURRENT_PC` after the evaluator has advanced past the opcode).
- **Status**: Implemented.
- **Stack**: Pushes the LOOP node, which owns its segment. Break/continue use `break_pc` (derived at runtime) and `continue_pc` (next instruction after PUSH_LOOP). Initializes node result to `None`.
- **Pushes:** KOS_DYN_LOOP (static extent KOS_EXT_LOOP; with its own segment for the loop body).
- **Invariants**: A KOS_EXT_LOOP extent must exist at the PUSH_LOOP PC; otherwise RuntimeError. Extent kind KOS_EXT_LOOP.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_LOOP)` → `ext->pop_pc + 1`.

**Example:** while loop. Break target derived from extent table (pop_pc+1 = 6).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | PUSH_LOOP | 0   | 0        | push LOOP region; break_pc = pop_pc+1 from extent table; continue_pc = 1 |
| 1     | CONST     | 0   | 0 (False)| condition |
| 2     | JUMP_FALSE| 5   | 5 (-> 5) | exit loop if false; jumps to POP_LOOP |
| 3     | …         | …   | …        | body |
| 4     | JUMP      | 1   | 1 (-> 1) | continue (loop head) |
| 5     | POP_LOOP  | 0   | 0        | |
| 6     | LABEL     | —   | —        | break target (= pop_pc+1) |

