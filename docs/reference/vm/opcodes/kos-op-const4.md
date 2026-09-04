# KOS_OP_CONST4
- **Style**: EXTRA (CONST, CONST, CONST, CONST). Row = four constant pool indices (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., a, b, c, d]`. Push the four constants in row order. A constant is
  a borrowed pool reference, so a failed push leaves nothing to release.
- **Invariants**: Indices in the constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs` (the trailing pack phase), which combines a
  run of consecutive bare CONST loads: quads, then one triple or one pair,
  then a bare load for a remainder of one. Constant folding runs earlier,
  so only loads the folder kept apart, such as a call's arguments, pack.

**Example:** Load constants a, b, c, d. Pool: 0 = a, 1 = b, 2 = c, 3 = d.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST4 | 0   | (0 (a), 1 (b), 2 (c), 3 (d)) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |
