# KOS_OP_MATCH_COLLECT_VAR / KOS_OP_MATCH_COLLECT_FAST

- **Style**: EXTRA (LABEL, slot, slot)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Append every remaining element of the cursor in `a1`, boxed
  from a pair spine, to the list in slot `a2`, and leave the cursor at its end.
  A pair spine is counted first, so an improper tail is a miss before anything
  is appended.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
