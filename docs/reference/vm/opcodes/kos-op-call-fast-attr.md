# KOS_OP_CALL_FAST_ATTR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: `[..., arg_0, ..., arg_{N-1}] → [..., result]`. Same resolution and invocation rules as [`KOS_OP_CALL_ATTR`](kos-op-call-attr.md), but `obj` is **not** on the stack: it is loaded from the current frame namespace via `KosNamespace_FastDeref` (slot `a0`). Requires a namespace that supports fast locals (`KosNamespace_SupportsFast`). When `send_self=0` (unbound method), uses `KosStackSteal_TailPrepend` to prepend `obj` to the stolen args; when `send_self=1`, uses `KosStackSteal_Tail` for `N` args only.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR.c): `obj` is an owned deref from the fast slot (`kos_ns_fast_deref` increfs); the handler releases it on every path, same discipline as `KOS_OP_CALL_VAR_ATTR`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

