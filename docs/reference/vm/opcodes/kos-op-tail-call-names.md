# KOS_OP_TAIL_CALL_NAMES

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = N (number of positional arguments).
- **Status**: Implemented.
- **Stack**: Same layout as CALL_NAMES: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple]`. Extracts via PopTailPtr (small) or PopTail (large); opcode owns storage, DECREFs at end. KosCallable: kos_callable_tail_vcall. Plain Python: kos_dyn_pop_frame when parent exists, PyObject_Vectorcall, kos_push_or_await. Returns **`KOS_OP_POST_CONTROL()`** (legacy **`KOS_OP_RES_CONTROL_RETURNED`**) when continuing.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
