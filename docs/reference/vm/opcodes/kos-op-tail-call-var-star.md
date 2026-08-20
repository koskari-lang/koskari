# KOS_OP_TAIL_CALL_VAR_STAR
- **Style**: EXTRA. Schema: VAR (name), STAR (bitflags), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same layout and pop order as CALL_VAR_STAR; call via `KosEvalState_CallArgs` with the tail flag set, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_STAR; lowers to TAIL_CALL_FAST_STAR.
