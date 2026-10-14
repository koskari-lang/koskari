# KOS_OP_FAST_CLEAR4
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = 4 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 4 fast slots in row order, as
  4 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

