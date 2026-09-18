# KOS_OP_TAIL_CALL_ATTR_NAMES
- **Style**: EXTRA. Schema: CONST (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_ATTR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_NAMES; never emitted by a special.
