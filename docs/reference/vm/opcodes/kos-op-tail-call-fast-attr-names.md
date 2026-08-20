# KOS_OP_TAIL_CALL_FAST_ATTR_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos). One cache slot.
- **Status**: Implemented.
- **Stack**: Same layout as CALL_FAST_ATTR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_NAMES; see KOS_OP_TAIL_CALL_FAST.
