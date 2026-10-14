# KOS_OP_MATCH_FIELD_KW

- **Style**: EXTRA (LABEL, CONST)
- **Stack**: `[..., elem]` -> `[..., elem, value]`; jump to `a0` with nothing
  pushed on a miss.
- **Semantics**: Peek. Push the attribute of `elem` that the constant `a1`
  names. A missing attribute is a miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
