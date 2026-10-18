# KOS_OP_MATCH_CORE

- **Style**: EXTRA (LABEL, RAW): label, core type ordinal.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the core type
  whose ordinal is `a1`: 0 `pair`, 1 `symbol`, 2 `keyword`, 3 `int`, 4 `str`,
  5 `tuple`, 6 `list`. An ordinal past those raises `SystemError`.
- **Compiler form:** Emitted by the match compiler for a type test against one
  of the seven core type names, which reads no variable.
