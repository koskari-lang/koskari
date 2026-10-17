# KOS_OP_MATCH_LITERAL

- **Style**: EXTRA (LABEL, CONST): label, constant.
- **Stack**: `[..., e]` -> `[...]`; jump to `a0` on a miss, `e` popped either
  way.
- **Semantics**: `e` read through `Datum` must be the constant `a1`, a gensym
  whose base is `a1`, or equal to `a1`.
- **Compiler form:** Emitted by the match compiler for a literal pattern,
  after the `MATCH_NEXT` that pulls the element.
