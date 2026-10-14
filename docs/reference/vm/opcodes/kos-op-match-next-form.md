# KOS_OP_MATCH_NEXT_FORM_VAR / KOS_OP_MATCH_NEXT_FORM_FAST

- **Style**: EXTRA (LABEL, slot, slot, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Pull the next element from the cursor in `a1`, apply the gate
  and the length precheck of `MATCH_FORM` with the widths in `a3`, and put the
  inner cursor into `a2`. The outer cursor advances only once the element has
  passed; at the end of the outer walk or on a failed gate nothing moves.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
