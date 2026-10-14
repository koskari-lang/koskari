# KOS_OP_MATCH_ITERATION / KOS_OP_MATCH_ITERATION_NAMED_VAR / KOS_OP_MATCH_ITERATION_NAMED_FAST

- **Style**: As `MATCH_CAPTURES`.
- **Stack**: `[...]` -> `[..., value]`.
- **Semantics**: Push one repeat iteration's value and clear the slots it read,
  so the next iteration starts with none of them bound. The value is the
  captures when any slot bound, else the span itself: one boxed element for a
  one-element span, a tuple of them otherwise.
- **Compiler form:** Emitted by the match compiler for a `compile/match` arm, a
  special form's pattern or a `syntax-rules` rule; the `_VAR` form pre-compile,
  the `_FAST` twin once `convert_fast` has lowered the slot.
