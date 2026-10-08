# KOS_OP_CALL_FAST_ATTR_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (opa bitflags), NOARG.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](kos-op-call-var-attr-star.md). `obj` is read from fast slot `a0`, so the frame must have fast locals. Pop, resolution and the call match CALL_ATTR_STAR.
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

