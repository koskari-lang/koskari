# KOS_OP_MATCH_SYNC

- **Style**: EXTRA (RAW, RAW): cursor depth, cursor depth.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor at depth `a0` to the position of the cursor
  at depth `a1`, a cursor over the same subject.
- **Compiler form:** Emitted by the match compiler at the end of a repeat,
  moving the sequence's cursor past the elements the repeat's bounded cursor
  took.
