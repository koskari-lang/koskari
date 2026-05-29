# KOS_OP_CHECK_IS_SUBCLASS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL (or COUNT). Arg = target pc.
- **Status**: Implemented.
- **Stack**: Class and type(s); jump if issubclass check fails. Stack after per implementation.
- **Invariants**: Jump locality for target.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
