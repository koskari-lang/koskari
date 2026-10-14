# KOS_OP_MATCH_LITERAL

- **Style**: EXTRA (LABEL, CONST)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. `elem` read through `Datum` must equal the constant
  `a1`, as `MATCH_NEXT_LITERAL` compares.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
