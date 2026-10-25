# KOS_OP_TAIL_CALL_VAR_STAR
- **Style**: EXTRA. Schema: VAR (name), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same layout and pop order as CALL_VAR_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_STAR; lowers to TAIL_CALL_FAST_STAR.
