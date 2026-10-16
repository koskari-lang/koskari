# KOS_OP_MATCH_REST_VAR / KOS_OP_MATCH_REST_FAST

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Consume the remainder of the cursor at depth `a1` but for the
  suffix width packed in the low eight bits of `a2`, requiring at least the
  minimum tail in the high eight bits. Too few elements, or an improper pair
  tail, is a miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
