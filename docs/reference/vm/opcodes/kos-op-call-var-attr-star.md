# KOS_OP_CALL_VAR_ATTR_STAR
- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG.
- **Stack**: Same semantics as [`KOS_OP_CALL_ATTR_STAR`](kos-op-call-attr-star.md), but `obj` is **not** on the stack; it is read from the frame namespace by name. Stack layout per opa (bottom … TOS): opa=0 → `[]`; opa=1 → `[pos]`; opa=2 → `[kwd]`; opa=3 → `[pos, kwd]` with `kwd` at TOS (same relative order as CALL_ATTR_STAR without the receiver). Pop order and `opa` handling match [`KOS_OP_CALL_VAR_STAR`](kos-op-call-var-star.md) combined with CALL_ATTR_STAR; `obj.name` resolves as CALL_ATTR describes, `obj` is prepended to the positional tuple when the attribute is an unbound function, and the callee is called with the tuple and the keywords.
- **Invariants**: `var_name` and attr name in constant pool; same opa bit meanings as CALL_ATTR_STAR.
- **See also**: [`KOS_OP_CALL_VAR_ATTR`](kos-op-call-var-attr.md) for the receiver read by name.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

