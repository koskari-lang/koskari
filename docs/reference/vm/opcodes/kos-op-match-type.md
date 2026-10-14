# KOS_OP_MATCH_TYPE_VAR / KOS_OP_MATCH_TYPE_FAST

- **Style**: EXTRA (LABEL, slot)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. `elem` read through `Datum` must be an instance of the
  type in slot `a1`.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
