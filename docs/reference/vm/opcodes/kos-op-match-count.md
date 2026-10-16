# KOS_OP_MATCH_COUNT

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: The list at depth `a1` must hold at least `a2` items: a repeat's
  minimum count, checked after its loop.
- **Compiler form:** Emitted by the match compiler after a repeat with a
  minimum count.
