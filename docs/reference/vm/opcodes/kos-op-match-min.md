# KOS_OP_MATCH_MIN

- **Style**: EXTRA (LABEL, RAW, RAW): label, list depth, count.
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Miss unless the list at depth `a1` holds at least `a2` items.
  Raises `TypeError` when that entry is not a list.
- **Compiler form:** Emitted by the match compiler after the last pass of a
  repeat with a minimum count.
