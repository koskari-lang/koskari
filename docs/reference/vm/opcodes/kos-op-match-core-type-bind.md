# KOS_OP_MATCH_CORE_TYPE_BIND_VAR / KOS_OP_MATCH_CORE_TYPE_BIND_FAST

- **Style**: EXTRA (LABEL, RAW, slot)
- **Stack**: `[..., elem]` -> `[..., elem]`; jump to `a0` on a miss.
- **Semantics**: `MATCH_CORE_TYPE` with the passing `elem` stored into slot
  `a2`.
- **Compiler form:** Emitted by the match compiler for a type test that names a
  core type, in a `compile/match` arm, a special form's pattern or a `syntax-
  rules` rule; the `_VAR` form pre-compile, the `_FAST` twin once
  `convert_fast` has lowered the slot.
