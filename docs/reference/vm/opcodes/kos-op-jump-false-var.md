# KOS_OP_JUMP_FALSE_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: No stack effect. Read the value the name resolves to in the
  current frame's namespace; if false, jump to the target, else advance.
  Raises `NameError` when the name is unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_FALSE` it feeds; a `CONST`
  feeder was folded in the squish. Lowers to
  [JUMP_FALSE_FAST](kos-op-jump-false-fast.md) when the name has a fast
  slot.

**Example:** `(when x ...)` with `x` resolved by name.

| index | opcode         | arg | arg view                | comment |
|------:|----------------|-----|-------------------------|---------|
| 0     | JUMP_FALSE_VAR | 0   | (v#0 ('x'), -> 3, 0, 0) | if x is false -> pc=3 |
| 1     | CONST          | 1   | 1 (1)                   | body |
| 2     | JUMP           | 4   | 4 (-> 4)                | |
| 3     | CONST          | 2   | 2 (None)                | |
| 4     | RETURN         | 1   | 1                       | |
