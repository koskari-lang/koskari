# KOS_OP_CALL_FAST_ATTR_STAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (opa bitflags), NOARG.
- **Status**: Implemented.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star). `obj` comes from `KosNamespace_FastDeref(ns, slot)` (slot `a0`); requires `KosNamespace_SupportsFast`. Pop/resolve/call semantics match CALL_ATTR_STAR. Same refcount convention as [`KOS_OP_CALL_FAST_ATTR`](#kos_op_call_fast_attr): no `Py_DECREF(obj)` on control paths (contrast [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star)).
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR_STAR.c): Parallel to CALL_FAST_ATTR / CALL_ATTR_STAR.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
