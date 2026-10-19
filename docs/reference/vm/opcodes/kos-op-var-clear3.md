# KOS_OP_VAR_CLEAR3
- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = 3 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 3 names in row order, as
  3 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` instructions (quads first, then one triple or one pair).

