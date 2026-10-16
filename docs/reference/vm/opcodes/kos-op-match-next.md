# KOS_OP_MATCH_NEXT_VAR / KOS_OP_MATCH_NEXT_FAST

- **Style**: EXTRA (LABEL, RAW)
- **Stack**: `[...]` -> `[..., elem]`; jump to `a0` with nothing pushed at the
  end of the walk.
- **Semantics**: Pull the next element from the cursor at depth `a1` and push it,
  boxed as a `Datum` with its cell's position when the cursor is a pair spine,
  raw from a sequence. The cursor advances. A cursor, mark or list is an entry on the operand stack the walk pushed; a RAW field names it by its depth under the top, 0 the top itself.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` and `_FAST`
  forms are one schema, since the row names no slot.
