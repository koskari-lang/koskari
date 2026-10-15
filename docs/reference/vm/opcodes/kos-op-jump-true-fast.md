# KOS_OP_JUMP_TRUE_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: No stack effect. Read the fast slot; if true, jump to the
  target, else advance. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_TRUE_VAR](kos-op-jump-true-var.md) when the name has a fast
  slot. `(unless x ...)` and an `or` clause of a threaded test over a
  bare variable take one dispatch.

**Example:** `(unless x ...)` with `x` in fast slot 0.

| index | opcode         | arg | arg view                   | comment |
|------:|----------------|-----|----------------------------|---------|
| 0     | JUMP_TRUE_FAST | 0   | (fast 0 ("x"), -> 3, 0, 0) | if x is true -> pc=3 |
| 1     | CONST          | 0   | 0 (1)                      | body |
| 2     | JUMP           | 4   | 4 (-> 4)                   | |
| 3     | CONST          | 1   | 1 (None)                   | |
| 4     | RETURN         | 1   | 1                          | |
