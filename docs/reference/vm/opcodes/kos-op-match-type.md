# KOS_OP_MATCH_TYPE_VAR / KOS_OP_MATCH_TYPE_FAST

- **Style**: EXTRA (LABEL, VAR) on `MATCH_TYPE_VAR`, (LABEL, FASTVAR) on
  `MATCH_TYPE_FAST`: label, type.
- **Stack**: `[..., e]` -> `[...]`; jump to `a0` on a miss, `e` popped either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the type in
  the variable `a1`, read by name on the `_VAR` form and by fast index on the
  `_FAST` form.
- **Compiler form:** Emitted by the match compiler for a type test and for the
  instance check of a class pattern with no positional fields; the `_VAR` form
  pre-compile, the `_FAST` twin once `convert_fast` has lowered the variable.
