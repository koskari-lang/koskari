# KOS_OP_MATCH_ITERATION / KOS_OP_MATCH_ITERATION_NAMED_VAR / KOS_OP_MATCH_ITERATION_NAMED_FAST

- **Style**: EXTRA (CONST, FASTVAR, RAW, RAW) on `MATCH_ITERATION`: the names
  tuple, the first capture slot of one run, the cursor's depth, the mode. EXTRA
  (CONST, RAW, RAW) on the NAMED forms: the names tuple, the cursor's depth, the
  mode.
- **Stack**: `[..., items, mark]` -> `[..., items, mark]` in mode 1, the value
  appended to `items`; `[..., mark]` -> `[..., mark, value]` in mode 0.
- **Semantics**: One repeat iteration's value, with the slots it read cleared so
  the next iteration starts with none of them bound. The value is the captures
  when any slot bound, else the span itself: one boxed element for a
  one-element span, a tuple of them otherwise. The iteration runs with its
  mark on the top of the stack and, in mode 1, the repeat's items list under
  that, which the value is appended to; in mode 0 the value is pushed, the
  form a capture over a wider pattern takes.
- **Compiler form:** Emitted by the match compiler at the end of each pass of a
  repeat, in mode 1, and for a capture over a pattern wider than one element,
  in mode 0.
