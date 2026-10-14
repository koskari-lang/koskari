# KOS_OP_MATCH_KEY_CONST_BIND_VAR / KOS_OP_MATCH_KEY_CONST_BIND_FAST / KOS_OP_MATCH_KEY_VAR_BIND_VAR / KOS_OP_MATCH_KEY_FAST_BIND_FAST

- **Style**: EXTRA (LABEL, key, slot)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: `MATCH_KEY` with the value stored into slot `a2` instead of
  pushed.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
