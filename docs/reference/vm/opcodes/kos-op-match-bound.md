# KOS_OP_MATCH_BOUND

- **Style**: EXTRA (LABEL, RAW, RAW): label, cursor depth, width.
- **Stack**: `[...]` -> `[..., bound]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Push a cursor at the position of the cursor at depth `a1`
  whose end is `a2` elements before that cursor's end. A miss when fewer than
  `a2` elements remain.
- **Compiler form:** Emitted by the match compiler at the start of a repeat,
  with `a2` the minimum width of what follows the repeat in the same sequence,
  so the repeat's passes never take an element that suffix needs.
