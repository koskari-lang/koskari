# KOS_OP_MATCH_NEXT_FORM_VAR / KOS_OP_MATCH_NEXT_FORM_FAST

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: `[...]` -> `[..., cursor]`; jump to `a0` with nothing pushed on a miss.
- **Semantics**: Pull the next element from the cursor at depth `a1`, apply the gate
  and the length precheck of `MATCH_FORM` with the widths in `a2`, and push the
  inner cursor. The outer entry is written before the push, so its depth
  still holds; at the end of the outer walk or on a failed gate nothing
  moves.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
