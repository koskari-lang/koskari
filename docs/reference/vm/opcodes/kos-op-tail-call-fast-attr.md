# KOS_OP_TAIL_CALL_FAST_ATTR
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_FAST_ATTR: args only; obj is loaded from the fast local slot (owned deref, released on every path). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR; see KOS_OP_TAIL_CALL_FAST.
