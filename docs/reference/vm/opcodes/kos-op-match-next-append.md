# KOS_OP_MATCH_NEXT_APPEND_VAR / KOS_OP_MATCH_NEXT_APPEND_FAST

- **Style**: EXTRA (LABEL, slot, slot)
- **Stack**: No stack effect; jump to `a0` at the end of the walk.
- **Semantics**: Pull the next element from the cursor in `a1` and append it,
  boxed as `MATCH_NEXT` boxes it, to the list in slot `a2`. The cursor
  advances.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
