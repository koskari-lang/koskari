# KOS_OP_CALL_FAST_NAMES

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: FASTVAR (slot index), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_VAR_NAMES. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
