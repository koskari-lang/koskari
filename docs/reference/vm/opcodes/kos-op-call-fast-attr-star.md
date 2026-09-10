# KOS_OP_CALL_FAST_ATTR_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (opa bitflags), NOARG.
- **Status**: Implemented.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](kos-op-call-var-attr-star.md). `obj` comes from `KosNamespace_FastDeref(ns, slot)` (slot `a0`); requires `KosNamespace_SupportsFast`. Pop/resolve/call semantics match CALL_ATTR_STAR. Same refcount convention as [`KOS_OP_CALL_FAST_ATTR`](kos-op-call-fast-attr.md): no `Py_DECREF(obj)` on control paths (contrast [`KOS_OP_CALL_VAR_ATTR_STAR`](kos-op-call-var-attr-star.md)).
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR_STAR.c): Parallel to CALL_FAST_ATTR / CALL_ATTR_STAR.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

