# KOS_OP_MATCH_CAPTURES / KOS_OP_MATCH_CAPTURES_NAMED_VAR / KOS_OP_MATCH_CAPTURES_NAMED_FAST

- **Style**: EXTRA (CONST, FASTVAR, RAW, RAW) on `MATCH_CAPTURES`: the
  names tuple, the first capture slot of one run, the start's depth, the
  cursor's depth. EXTRA (CONST, RAW, RAW) on the NAMED forms: the names tuple,
  the start's depth, the cursor's depth, each capture read by its name.
- **Stack**: `[...]` -> `[..., captures]`.
- **Semantics**: Push the captures of a scope: the value of each named slot
  that is bound, an unbound slot absent. The start is the subject for a
  matcher's root and the position a `MATCH_MARK` pushed for a group. When it
  is a pair cell the result is a `DatumFrame` whose source is the span from
  that cell to the cursor and whose position is the start cell's; when it is an
  index the result is a plain dict.
- **Compiler form:** Emitted by the match compiler at the end of a standalone
  matcher and for each named group; the NAMED forms for a `compile/match` arm,
  whose captures are the enclosing function's locals.
