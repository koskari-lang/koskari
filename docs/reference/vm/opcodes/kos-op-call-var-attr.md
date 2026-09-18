# KOS_OP_CALL_VAR_ATTR
- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: Same as `KOS_OP_CALL_FAST_ATTR`. `obj` is obtained with `frame_ns_deref(ctx, var_name)` (owned reference); the handler balances refcounts with `Py_DECREF(obj)` on exit paths.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR.c): Otherwise identical control flow to `KOS_OP_CALL_FAST_ATTR` / `KOS_OP_CALL_ATTR` (resolve via `kos_resolve_named_attr`, KosCallable vs plain Python, `TailPrepend` when `send_self=0`).
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

