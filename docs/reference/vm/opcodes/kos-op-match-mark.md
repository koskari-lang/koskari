# KOS_OP_MATCH_MARK_VAR / KOS_OP_MATCH_MARK_FAST

- **Style**: EXTRA (RAW)
- **Stack**: `[...]` -> `[..., position]`; cannot miss.
- **Semantics**: Push the position of the cursor at depth `a0`: the cell itself for
  a pair spine, the index as an int for a sequence. The rows that follow name
  the cursor one deeper while the mark lives; the path that ends the mark's
  extent pops it.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
