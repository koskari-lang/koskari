# KOS_OP_MATCH_RESET_VAR / KOS_OP_MATCH_RESET_FAST

- **Style**: EXTRA (RAW, RAW)
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor at depth `a0` back to the position of the
  mark at depth `a1`. A failed branch of a repeat, an optional or a `(or ...)`
  returns to where it began.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
