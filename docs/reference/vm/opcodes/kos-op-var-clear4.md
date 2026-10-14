# KOS_OP_VAR_CLEAR4
- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = 4 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 4 names in row order, as
  4 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` rows (quads first, then one triple or one pair); `convert_fast`
  lowers it to `FAST_CLEAR4` when every name has a fast slot.

