# KOS_OP_MATCH_NEXT_TYPE_VAR / KOS_OP_MATCH_NEXT_TYPE_FAST

- **Style**: EXTRA (LABEL, RAW, slot)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Pull the next element from the cursor at depth `a1`; the element
  read through `Datum` must be an instance of the type in slot `a2`. The cursor
  advances only on a pass.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
