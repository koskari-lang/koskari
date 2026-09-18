# KOS_OP_TAIL_CALL_VAR_NAMES
- **Style**: EXTRA. Schema: VAR (name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_VAR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_NAMES; lowers to TAIL_CALL_FAST_NAMES.
