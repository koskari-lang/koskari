# KOS_OP_FAST_CLEAR3
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = 3 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 3 fast slots in row order, as
  3 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

