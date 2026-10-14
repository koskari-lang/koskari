# KOS_OP_MATCH_SAME_VAR / KOS_OP_MATCH_SAME_FAST

- **Style**: EXTRA (LABEL, slot)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. `elem` must equal the value bound in slot `a1`, both
  read through `Datum`: identity, then `==`. An unbound slot is a miss, and so
  is a `DatumFrame` on either side.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
