# KOS_OP_TAIL_CALL_FAST_ATTR_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), COUNT (opa bitflags), NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_FAST_ATTR_STAR; call via `KosEvalState_CallArgs` with the tail flag set, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_STAR; see KOS_OP_TAIL_CALL_FAST.
