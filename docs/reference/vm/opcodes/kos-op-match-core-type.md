# KOS_OP_MATCH_CORE_TYPE

- **Style**: EXTRA (LABEL, RAW)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: Peek. `elem` read through `Datum` must be an instance of the
  core type whose ordinal is `a1`, as `MATCH_NEXT_CORE_TYPE` tests.
- **Compiler form:** Emitted by the match compiler for a type test that names a
  core type, in a `compile/match` arm, a special form's pattern or a `syntax-
  rules` rule; the `_VAR` form pre-compile, the `_FAST` twin once
  `convert_fast` has lowered the slot.
