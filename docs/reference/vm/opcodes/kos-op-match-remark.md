# KOS_OP_MATCH_REMARK

- **Style**: EXTRA (RAW, RAW)
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the mark at depth `a1` to the position of the cursor at
  depth `a0`. A repeat pushes its mark once before its loop and re-marks at
  the head of every pass, so a pass over a sequence allocates nothing.
- **Compiler form:** Emitted by the match compiler for a repeat in a
  `compile/match` arm, a special form's pattern or a `syntax-rules` rule.
