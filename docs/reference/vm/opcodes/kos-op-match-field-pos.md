# KOS_OP_MATCH_FIELD_POS_VAR / KOS_OP_MATCH_FIELD_POS_FAST

- **Style**: EXTRA (LABEL, VAR, RAW) on `MATCH_FIELD_POS_VAR`, (LABEL,
  FASTVAR, RAW) on `MATCH_FIELD_POS_FAST`: label, class, position.
- **Stack**: `[..., o]` -> `[..., o, a]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the attribute of `o`, read through `Datum`, that
  `__match_args__[a2]` of the class in the variable `a1` names; the variable
  is read by name on the `_VAR` form and by fast index on the `_FAST` form. A
  missing attribute is a miss. For a self-matching builtin class (as on
  `MATCH_CLASS`) with no `__match_args__`, `o` itself is pushed. For any
  other class, a missing `__match_args__`, a `__match_args__` that is not a
  tuple, a position past its end, or an entry that is not a `str` raises
  `TypeError`. On Python 3.9 a dataclass or named tuple without
  `__match_args__` reads the equivalent tuple Python 3.10 would generate.
- **Compiler form:** Emitted by the match compiler for each positional field
  of a class pattern; the `_VAR` form pre-compile, the `_FAST` twin once
  `convert_fast` has lowered the variable.
