# KOS_OP_JUMP_FALSE_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: No stack effect. Read the fast slot; if false, jump to the
  target, else advance. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_FALSE_VAR](kos-op-jump-false-var.md) when the name has a fast
  slot. `(if x ...)`, `(when x ...)`, `(while flag ...)` and a
  bare-variable clause of a threaded `and`/`or` test take one dispatch.

**Example:** `(when x ...)` with `x` in fast slot 0.

| index | opcode          | arg | arg view                   | comment |
|------:|-----------------|-----|----------------------------|---------|
| 0     | JUMP_FALSE_FAST | 0   | (fast 0 ("x"), -> 3, 0, 0) | if x is false -> pc=3 |
| 1     | CONST           | 0   | 0 (1)                      | body |
| 2     | JUMP            | 4   | 4 (-> 4)                   | |
| 3     | CONST           | 1   | 1 (None)                   | |
| 4     | RETURN          | 1   | 1                          | |
