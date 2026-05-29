# KOS_OP_LOCALS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., locals_dict]`.
- **Invariants**: Frame has locals namespace.

**Example:** Push frame's locals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | LOCALS | 0   | 0        | stack: [..., locals_dict] |
| 1     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
