# KOS_OP_MATCH_PEEK

- **Style**: EXTRA (LABEL, RAW)
- **Stack**: `[...]` -> `[..., elem]`; jump to `a0` with nothing pushed at the
  end of the walk.
- **Semantics**: Push the next element of the cursor at depth `a1` as `MATCH_NEXT`
  does, and leave the cursor where it is. A zero-width `(not ...)` reads its
  element this way.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule.
