# KOS_OP_MATCH_NEXT_LITERAL_VAR / KOS_OP_MATCH_NEXT_LITERAL_FAST

- **Style**: EXTRA (LABEL, slot, CONST)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Pull the next element from the cursor in `a1`; the element
  read through `Datum` must equal the constant `a2`. A scoped symbol equals the
  symbol it was derived from; otherwise `==`. The cursor advances only on a
  pass.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
