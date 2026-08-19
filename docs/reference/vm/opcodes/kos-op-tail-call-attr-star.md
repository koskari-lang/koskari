# KOS_OP_TAIL_CALL_ATTR_STAR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG. One cache slot.
- **Status**: Implemented.
- **Stack**: Same layout and pop order as CALL_ATTR_STAR; call via `KosEvalState_CallArgs` with the tail flag set, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_STAR; never emitted by a special.
