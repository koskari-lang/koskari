# KOS_OP_MATCH_RESET

- **Style**: EXTRA (RAW, RAW): cursor depth, mark depth.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor at depth `a0` back to the position of the
  mark at depth `a1`.
- **Compiler form:** Emitted by the match compiler where a branch of an
  `(or ...)`, an optional, a negation or a repeat pass ends, returning the
  cursor to where it began.
