# KOS_OP_JUMP_TRUE_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: No stack effect. Read the value the name resolves to in the
  current frame's namespace; if true, jump to the target, else advance.
  Raises `NameError` when the name is unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_TRUE` it feeds; a `CONST`
  feeder was folded in the squish. Lowers to
  [JUMP_TRUE_FAST](kos-op-jump-true-fast.md) when the name has a fast
  slot.

**Example:** `(if (not x) 1 2)` with `x` resolved by name.

| index | opcode        | arg | arg view                | comment |
|------:|---------------|-----|-------------------------|---------|
| 0     | JUMP_TRUE_VAR | 0   | (v#0 ('x'), -> 3, 0, 0) | if x is true -> pc=3 |
| 1     | CONST         | 1   | 1 (1)                   | then arm |
| 2     | JUMP          | 4   | 4 (-> 4)                | |
| 3     | CONST         | 2   | 2 (2)                   | else arm |
| 4     | RETURN        | 1   | 1                       | |
