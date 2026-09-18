# KOS_OP_VAR3
- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = three constant pool name indices
  (post-compile).
- **Stack**: `[...] → [..., a, b, c]`. Load the three names from the frame namespace and
  push in row order. Every name resolves before any push, so a missing name
  errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As VAR2: produced only by `ByteCodeProcessor.pack_runs`
  from a run of three consecutive bare VAR loads (quads first, then one triple
  or one pair). `convert_fast` lowers to FAST3 when all three names have fast
  slots (all-or-nothing; a mixed-eligibility row stays VAR3).

**Example:** Load "a", "b", "c". Pool: 0-2 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR3   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |
