# KOS_OP_TAIL_CALL_ATTR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG. One cache slot (attr resolution).
- **Status**: Implemented.
- **Stack**: Same layout as CALL_ATTR: `[..., obj, arg_0, ..., arg_{N-1}]`. Resolve `obj.name` via `kos_resolve_named_attr`, then call in tail position: the caller's frame is reused rather than a new frame pushed (`KOS_CALL_TAIL` through `kos_call_invoke`; an exact non-yielding KosFunction takes the bind-before-reset subpath, every other callable class spills and keeps `KosEvalState_Call`'s tail semantics).
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL_ATTR in function-body code; never emitted by a special.
