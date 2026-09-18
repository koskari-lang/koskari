# KOS_OP_TAIL_CALL_FAST_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_FAST_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_NAMES; see KOS_OP_TAIL_CALL_FAST.
