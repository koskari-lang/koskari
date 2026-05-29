# KOS_OP_VAR_ATTR_STORE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set attribute. Equivalent to ATTR_STORE with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_STORE.c`

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
