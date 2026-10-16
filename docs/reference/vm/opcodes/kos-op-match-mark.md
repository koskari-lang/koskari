# KOS_OP_MATCH_MARK

- **Style**: EXTRA (RAW)
- **Stack**: `[...]` -> `[..., position]`; cannot miss.
- **Semantics**: Push a mark at the position of the cursor at depth `a0`: the
  cell of a pair spine, or a mark object over the sequence at the cursor's
  index. The instructions that follow name the cursor one deeper while the
  mark lives; the path that ends the mark's extent pops it.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule.
