# KOS_OP_MATCH_NEXT_BIND_VAR / KOS_OP_MATCH_NEXT_BIND_FAST

- **Style**: EXTRA (LABEL, RAW, slot)
- **Stack**: No stack effect; jump to `a0` at the end of the walk.
- **Semantics**: Pull the next element from the cursor at depth `a1` and store it,
  boxed as `MATCH_NEXT` boxes it, into slot `a2`. The cursor advances.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
