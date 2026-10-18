# KOS_OP_MATCH_CLASS_VAR / KOS_OP_MATCH_CLASS_FAST

- **Style**: EXTRA (LABEL, VAR, RAW) on `MATCH_CLASS_VAR`, (LABEL, FASTVAR,
  RAW) on `MATCH_CLASS_FAST`: label, class, positional field count.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the class in
  the variable `a1`, read by name on the `_VAR` form and by fast index on the
  `_FAST` form; a non-instance is a miss. The class's `__match_args__` must
  then hold at least `a2` names: a class with no `__match_args__`, a
  `__match_args__` that is not a tuple, or one shorter than `a2` raises
  `TypeError` before any attribute of `e` is read. On Python 3.9 a dataclass
  or named tuple without `__match_args__` reads the equivalent tuple Python
  3.10 would generate.
- **Compiler form:** Emitted by the match compiler for the instance check of a
  class pattern with positional fields, `a2` their count; the `_VAR` form
  pre-compile, the `_FAST` twin once `convert_fast` has lowered the variable.
