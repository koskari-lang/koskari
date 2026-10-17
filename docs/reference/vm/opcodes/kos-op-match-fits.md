# KOS_OP_MATCH_FITS

- **Style**: EXTRA (LABEL, RAW, RAW, RAW): label, cursor depth, minimum,
  maximum.
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Miss unless the cursor at depth `a1` has at least `a2` and at
  most `a3` elements left before its end; a maximum of `0xFFFF` is unbounded.
  With `a2` and `a3` both 0 it is the end test.
- **Compiler form:** Emitted by the match compiler as the length check of a
  sequence, a matcher's root and a composite pattern ahead of their elements'
  tests, as the end test after each walk, and at the head of each repeat pass.
