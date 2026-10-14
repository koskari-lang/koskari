# KOS_OP_MATCH_REST_VAR / KOS_OP_MATCH_REST_FAST

- **Style**: EXTRA (LABEL, slot, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Consume the remainder of the cursor in `a1` but for the suffix
  width packed in the low eight bits of `a2`, requiring at least the minimum
  tail in the high eight bits. Too few elements, or an improper pair tail, is a
  miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
