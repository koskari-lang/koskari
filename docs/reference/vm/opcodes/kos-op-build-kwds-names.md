# KOS_OP_BUILD_KWDS_NAMES

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of names tuple.
- **Status**: Implemented.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., dict]`. Pop n values, map names[i] → value[i], push dict. Names tuple from constant pool; values from stack (val_0 deepest, val_{n-1} at TOS).

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
