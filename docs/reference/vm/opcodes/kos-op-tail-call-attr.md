# KOS_OP_TAIL_CALL_ATTR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG. One cache slot (attr resolution).
- **Stack**: Same layout as CALL_ATTR: `[..., obj, arg_0, ..., arg_{N-1}]`. `obj.name` resolves as CALL_ATTR describes, then the call is made in tail position: a Koskari function takes over the caller's frame rather than a new frame being pushed, and any other callable is called as TAIL_CALL describes.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL_ATTR in function-body code; never emitted by a special.
