# KOS_OP_MATCH_PEEK_VAR / KOS_OP_MATCH_PEEK_FAST

- **Style**: EXTRA (LABEL, slot)
- **Stack**: `[...]` -> `[..., elem]`; jump to `a0` with nothing pushed at the
  end of the walk.
- **Semantics**: Push the next element of the cursor in `a1` as `MATCH_NEXT`
  does, and leave the cursor where it is. A zero-width `(not ...)` reads its
  element this way.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
