# KOS_OP_CALL_FAST_ATTR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: `[..., arg_0, ..., arg_{N-1}] → [..., result]`. Same resolution and invocation rules as [`KOS_OP_CALL_ATTR`](kos-op-call-attr.md), but `obj` is **not** on the stack: it is read from the current frame's fast slot `a0`, so the frame must have fast locals. An unbound function found on the class receives `obj` as its first argument; any other callable receives the `N` arguments alone.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

