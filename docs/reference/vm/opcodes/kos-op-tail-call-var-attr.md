# KOS_OP_TAIL_CALL_VAR_ATTR
- **Style**: EXTRA. Schema: VAR (name for obj), CONST (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_VAR_ATTR: args only; obj is loaded from the frame namespace by name (owned deref, released on every path). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR; lowers to TAIL_CALL_FAST_ATTR.
