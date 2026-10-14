# KOS_OP_MATCH_FIELD_POS_VAR / KOS_OP_MATCH_FIELD_POS_FAST

- **Style**: EXTRA (LABEL, slot, RAW)
- **Stack**: `[..., elem]` -> `[..., elem, value]`; jump to `a0` with nothing
  pushed on a miss.
- **Semantics**: Peek. Push the attribute of `elem` that entry `a2` of the
  `__match_args__` of the class in slot `a1` names. A missing attribute is a
  miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
