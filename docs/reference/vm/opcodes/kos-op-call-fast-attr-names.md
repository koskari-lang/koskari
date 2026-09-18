# KOS_OP_CALL_FAST_ATTR_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot index for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Obj is loaded from fast slot `a0` via `KosNamespace_FastDeref`; not on stack. One fewer element than CALL_ATTR_NAMES. Resolve and call like CALL_ATTR_NAMES. When `send_self=0` (unbound method), uses `KosStackSteal_TailPrepend` to prepend obj to args; when `send_self=1`, uses `KosStackSteal_Tail`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

