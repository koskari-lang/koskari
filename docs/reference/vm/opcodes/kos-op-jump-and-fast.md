# KOS_OP_JUMP_AND_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the fast slot; if false, push the value and jump
  to the target; if true, advance with nothing pushed and no reference
  taken. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_AND_VAR](kos-op-jump-and-var.md) when the name has a fast slot.

**Example:** `(and x (f x))` in value position, `x` in fast slot 0.

| index | opcode        | arg | arg view                   | comment |
|------:|---------------|-----|----------------------------|---------|
| 0     | JUMP_AND_FAST | 0   | (fast 0 ("x"), -> 4, 0, 0) | x false: push x, pc=4 |
| 1     | VAR           | 0   | 0 ('f')                    | stack: [..., f] |
| 2     | FAST          | 0   | 0 (local "x")              | stack: [..., f, x] |
| 3     | CALL          | 1   | 1                          | stack: [..., (f x)] |
| 4     | RETURN        | 1   | 1                          | |
