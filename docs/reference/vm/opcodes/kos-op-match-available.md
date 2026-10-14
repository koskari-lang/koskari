# KOS_OP_MATCH_AVAILABLE_VAR / KOS_OP_MATCH_AVAILABLE_FAST

- **Style**: EXTRA (LABEL, slot, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: At least `a2` elements must remain from the cursor in `a1`.
  The reservation a fixed suffix makes before a repeat, an optional or a branch
  consumes elements.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
