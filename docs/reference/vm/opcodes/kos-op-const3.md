# KOS_OP_CONST3
- **Style**: EXTRA (CONST, CONST, CONST, NOARG). Row = three constant pool indices (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b, c]`. Push the three constants in row order. A constant is
  a borrowed pool reference, so a failed push leaves nothing to release.
- **Invariants**: Indices in the constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs` (the trailing pack phase), which combines a
  run of consecutive bare CONST loads: quads, then one triple or one pair,
  then a bare load for a remainder of one. Constant folding runs earlier,
  so only loads the folder kept apart, such as a call's arguments, pack.

**Example:** Load constants a, b, c. Pool: 0 = a, 1 = b, 2 = c.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST3 | 0   | (0 (a), 1 (b), 2 (c), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |
