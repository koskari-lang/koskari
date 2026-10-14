# KOS_OP_MATCH_MAP

- **Style**: LABEL
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to the label on a miss.
- **Semantics**: Peek. The mapping gate on `elem` read through `Datum`: a dict
  or a mapping-typed object passes; anything else misses.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
