# KOS_OP_MATCH_END_VAR / KOS_OP_MATCH_END_FAST

- **Style**: EXTRA (LABEL, slot)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: The cursor in `a1` must be at its end: `nil` for a pair spine,
  the length for a sequence. An improper tail is a miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
