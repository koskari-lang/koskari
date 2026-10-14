# KOS_OP_MATCH_MARK_VAR / KOS_OP_MATCH_MARK_FAST

- **Style**: EXTRA (slot, slot)
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Store the position of the cursor in `a0` into slot `a1`: the
  cell itself for a pair spine, the index as an int for a sequence.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
