# KOS_OP_MATCH_CAPTURES / KOS_OP_MATCH_CAPTURES_NAMED_VAR / KOS_OP_MATCH_CAPTURES_NAMED_FAST

- **Style**: EXTRA (CONST, FASTVAR, FASTVAR, FASTVAR) on `MATCH_CAPTURES`: the
  names tuple, the first capture slot of one run, the start, the cursor. EXTRA
  (CONST, slot, slot) on the NAMED forms: the names tuple, the start, the
  cursor, each capture read by its name.
- **Stack**: `[...]` -> `[..., captures]`.
- **Semantics**: Push the captures of a scope: the value of each named slot
  that is bound, an unbound slot absent. When the start saved by `MATCH_MARK`
  is a pair cell the result is a `DatumFrame` whose source is the span from
  that cell to the cursor and whose position is the start cell's; when it is an
  index the result is a plain dict.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
