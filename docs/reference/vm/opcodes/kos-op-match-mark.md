# KOS_OP_MATCH_MARK_VAR / KOS_OP_MATCH_MARK_FAST

- **Style**: EXTRA (RAW)
- **Stack**: `[...]` -> `[..., position]`; cannot miss.
- **Semantics**: Push the cursor at depth `a0` as its position: the cell of a
  pair spine or the sequence cursor, which no advance moves while the mark
  holds it. The instructions that follow name the cursor one deeper while the mark
  lives; the path that ends the mark's extent pops it.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
