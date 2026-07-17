# KOS_OP_VAR2
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = two constant pool name
  indices (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b]`. Load both names from the frame namespace
  and push in row order. Both names resolve before either push, so a missing
  name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  `Compiler.pack_var_runs` (the late `compress` sweep), which combines a run
  of two consecutive bare VAR loads. `convert_fast` lowers to FAST2 when
  both names have fast slots (all-or-nothing; a mixed-eligibility row stays
  VAR2).

**Example:** Load "x" then "y". Pool: 0 = "x", 1 = "y".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR2   | 0   | (0 ('x'), 1 ('y'), 0, 0) | stack: [..., x, y] |
| 1     | RETURN | 1   | 1        | |
