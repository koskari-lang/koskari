# KOS_OP_JUMP_OR_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the value the name resolves to in the current
  frame's namespace; if true, push it and jump to the target; if false,
  advance with nothing pushed. Raises `NameError` when the name is
  unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_OR` it feeds. Lowers to
  [JUMP_OR_FAST](kos-op-jump-or-fast.md) when the name has a fast slot.

**Example:** `(or x default)` in value position, both resolved by name.

| index | opcode      | arg | arg view                | comment |
|------:|-------------|-----|-------------------------|---------|
| 0     | JUMP_OR_VAR | 0   | (v#0 ('x'), -> 2, 0, 0) | x true: push x, pc=2 |
| 1     | VAR         | 1   | 1 ('default')           | stack: [..., default] |
| 2     | LABEL       | --  | --                      | |
| 3     | RETURN      | 1   | 1                       | |
