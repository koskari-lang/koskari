# KOS_OP_VAR4
- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = four constant pool name
  indices (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b, c, d]`. Load all four names from the frame
  namespace and push in row order. All names resolve before any push, so a
  missing name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As VAR2: produced only by `Compiler.pack_var_runs` from
  a run of four (or more, greedily) consecutive bare VAR loads.
  `convert_fast` lowers to FAST4 when all four names have fast slots.

**Example:** Load "a", "b", "c", "d". Pool: 0-3 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR4   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 3 ('d')) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |
