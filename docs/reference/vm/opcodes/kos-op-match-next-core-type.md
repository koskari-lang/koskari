# KOS_OP_MATCH_NEXT_CORE_TYPE

- **Style**: EXTRA (LABEL, RAW, RAW)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Pull the next element from the cursor at depth `a1`; the element
  read through `Datum` must be an instance of the core type whose ordinal is
  `a2`: `pair`, `symbol`, `keyword`, `int`, `str`, `tuple` or `list`,
  subclasses included, so a scoped symbol passes `symbol` and a bool passes
  `int`. Nothing is read from the namespace; a binding of the type's name in
  scope is never consulted. The cursor advances only on a pass.
- **Compiler form:** Emitted by the match compiler for a type test that names a
  core type, in a `compile/match` arm, a special form's pattern or a `syntax-
  rules` rule.
