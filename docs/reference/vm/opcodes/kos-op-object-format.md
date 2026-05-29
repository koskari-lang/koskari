# KOS_OP_OBJECT_FORMAT

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: format operation. Arg must be 1 or 2.
  - arg=1: pop object, call `PyObject_Format(obj, NULL)`.
  - arg=2: pop rules then object, call `PyObject_Format(obj, rules)`.
  Stack effect: `[..., obj] -> [..., str]` or `[..., obj, rules] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT.c`

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
