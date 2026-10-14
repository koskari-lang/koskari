# KOS_OP_MATCH_REST_BIND_VAR / KOS_OP_MATCH_REST_BIND_FAST

- **Style**: EXTRA (LABEL, slot, RAW, slot)
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: `MATCH_REST` with the remainder stored into slot `a3`: the
  tail cell boxed as a `Datum` for a pair spine, a tuple slice for a sequence.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
