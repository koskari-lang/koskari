# KOS_OP_MATCH_NEXT_FORM

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: `[...]` -> `[..., cursor]`; jump to `a0` with nothing pushed on a miss.
- **Semantics**: Pull the next element from the cursor at depth `a1`, apply the gate
  and the length precheck of `MATCH_FORM` with the widths in `a2`, and push the
  inner cursor. The outer entry is written before the push, so its depth
  still holds; at the end of the outer walk or on a failed gate nothing
  moves.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule.
