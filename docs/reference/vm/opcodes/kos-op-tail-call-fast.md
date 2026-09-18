# KOS_OP_TAIL_CALL_FAST
- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Stack**: Same layout as CALL_FAST; the callable is loaded from the fast local slot (owned deref). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Reached through promotion of CALL_VAR plus the fast lowering, or promotion of an already-lowered CALL_FAST; never emitted by a special.
