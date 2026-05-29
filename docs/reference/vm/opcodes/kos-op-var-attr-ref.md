# KOS_OP_VAR_ATTR_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., attr_ref]`. Push attribute reference. Equivalent to ATTR_REF with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_REF.c`


## 13. Fast variables

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
