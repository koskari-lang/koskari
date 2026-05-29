# KOS_OP_CALL_VAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: `[..., pos_0, ..., pos_{N-1}] → [..., result]`. Callable is loaded from frame namespace by name (`frame_ns_deref`); not on stack. KosCallable: steal args then KosContext_Call. Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
