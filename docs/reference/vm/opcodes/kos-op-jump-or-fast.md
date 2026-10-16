# KOS_OP_JUMP_OR_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the fast slot; if true, push the value and jump to
  the target; if false, advance with nothing pushed and no reference
  taken. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_OR_VAR](kos-op-jump-or-var.md) when the name has a fast slot.

**Example:** `(or x default)` in value position, both in fast slots.

| index | opcode       | arg | arg view                   | comment |
|------:|--------------|-----|----------------------------|---------|
| 0     | JUMP_OR_FAST | 0   | (fast 0 ("x"), -> 2, 0, 0) | x true: push x, pc=2 |
| 1     | FAST         | 1   | 1 (local "default")        | stack: [..., default] |
| 2     | RETURN       | 1   | 1                          | |
