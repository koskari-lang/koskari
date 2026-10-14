# KOS_OP_MATCH_NEXT_VAR / KOS_OP_MATCH_NEXT_FAST

- **Style**: EXTRA (LABEL, slot)
- **Stack**: `[...]` -> `[..., elem]`; jump to `a0` with nothing pushed at the
  end of the walk.
- **Semantics**: Pull the next element from the cursor in `a1` and push it,
  boxed as a `Datum` with its cell's position when the cursor is a pair spine,
  raw from a sequence. The cursor advances.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
