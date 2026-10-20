# KOS_OP_VAR_CLEAR2
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = 2 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 2 names in row order, as
  2 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` instructions (quads first, then one triple or one pair).

