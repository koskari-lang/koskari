# KOS_OP_MATCH_AVAILABLE_VAR / KOS_OP_MATCH_AVAILABLE_FAST

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: At least `a2` elements must remain from the cursor at depth `a1`.
  The reservation a fixed suffix makes before a repeat, an optional or a branch
  consumes elements.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
