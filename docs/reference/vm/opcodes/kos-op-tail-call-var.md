# KOS_OP_TAIL_CALL_VAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same layout as CALL_VAR: `[..., pos_0, ..., pos_{N-1}]`; the callable is loaded from the frame namespace by name, not on the stack. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR; convert_fast lowers it to TAIL_CALL_FAST when the name has a fast slot.
