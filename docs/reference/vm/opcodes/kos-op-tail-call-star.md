# KOS_OP_TAIL_CALL_STAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Status**: Implemented.
- **Stack**: Same layout as CALL_STAR: `[..., callable, pos, kwd]`. Pop order and ensure_tuple as CALL_STAR. KosCallable: kos_callable_tail_call. Plain Python: kos_dyn_pop_frame when parent exists, PyObject_Call, kos_push_or_await. Returns **`KOS_OP_POST_CONTROL()`** (legacy **`KOS_OP_RES_CONTROL_RETURNED`**) when continuing. Same replace-frame semantics as TAIL_CALL.

---


## 18. Block control

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
