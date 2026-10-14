# KOS_OP_MATCH_FIELD_POS_BIND_VAR / KOS_OP_MATCH_FIELD_POS_BIND_FAST

- **Style**: EXTRA (LABEL, slot, RAW, slot)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: `MATCH_FIELD_POS` with the attribute stored into slot `a3`
  instead of pushed.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
