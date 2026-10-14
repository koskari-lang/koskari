# KOS_OP_FAST_CLEAR2
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = 2 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 2 fast slots in row order, as
  2 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

