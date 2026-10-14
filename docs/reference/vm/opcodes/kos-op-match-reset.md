# KOS_OP_MATCH_RESET_VAR / KOS_OP_MATCH_RESET_FAST

- **Style**: EXTRA (slot, slot)
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor in `a0` back to the position saved in slot
  `a1`. A failed branch of a repeat, an optional or a `(or ...)` returns to
  where it began.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
