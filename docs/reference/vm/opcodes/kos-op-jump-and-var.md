# KOS_OP_JUMP_AND_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the value the name resolves to in the current
  frame's namespace; if false, push it and jump to the target; if true,
  advance with nothing pushed. Raises `NameError` when the name is
  unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_AND` it feeds. Lowers to
  [JUMP_AND_FAST](kos-op-jump-and-fast.md) when the name has a fast
  slot.

**Example:** `(and x (f x))` in value position, `x` resolved by name.

| index | opcode       | arg | arg view                | comment |
|------:|--------------|-----|-------------------------|---------|
| 0     | JUMP_AND_VAR | 0   | (v#0 ('x'), -> 4, 0, 0) | x false: push x, pc=4 |
| 1     | VAR2         | 1   | (f, x)                  | stack: [..., f, x] |
| 2     | CALL         | 1   | 1                       | stack: [..., (f x)] |
| 3     | LABEL        | --  | --                      | |
| 4     | RETURN       | 1   | 1                       | |
