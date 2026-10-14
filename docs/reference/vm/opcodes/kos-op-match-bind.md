# KOS_OP_MATCH_BIND_VAR / KOS_OP_MATCH_BIND_FAST

- **Style**: VAR / FASTVAR (the slot)
- **Stack**: `[..., elem]` -> `[..., elem]`.
- **Semantics**: Peek. Store `elem` into the slot; nothing is popped and
  nothing can miss.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
