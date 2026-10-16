# KOS_OP_MATCH_NEXT_APPEND

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: No stack effect; jump to `a0` at the end of the walk.
- **Semantics**: Pull the next element from the cursor at depth `a1` and append it,
  boxed as `MATCH_NEXT` boxes it, to the list at depth `a2`. The cursor
  advances.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule.
