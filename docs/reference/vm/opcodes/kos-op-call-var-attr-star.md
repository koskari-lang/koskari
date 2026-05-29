# KOS_OP_CALL_VAR_ATTR_STAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG.
- **Status**: Implemented.
- **Stack**: Same semantics as [`KOS_OP_CALL_ATTR_STAR`](#kos_op_call_attr_star), but `obj` is **not** on the stack; it is loaded with `frame_ns_deref(ctx, var_name)` (owned reference; handler decrements on exit paths). Stack layout per opa (bottom … TOS): opa=0 → `[]`; opa=1 → `[pos]`; opa=2 → `[kwd]`; opa=3 → `[pos, kwd]` with `kwd` at TOS (same relative order as CALL_ATTR_STAR without the receiver). Pop order and `opa` handling match [`KOS_OP_CALL_VAR_STAR`](#kos_op_call_var_star) combined with CALL_ATTR_STAR; resolve via `kos_resolve_named_attr`, prepend `obj` to tuple args when `send_self=0`, call via `KosContext_CallArgs`.
- **Invariants**: `var_name` and attr name in constant pool; same opa bit meanings as CALL_ATTR_STAR.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR_STAR.c): Parallel to CALL_ATTR_STAR with VAR receiver semantics (see [`KOS_OP_CALL_VAR_ATTR`](#kos_op_call_var_attr)).
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
