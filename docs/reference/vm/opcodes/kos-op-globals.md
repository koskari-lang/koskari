# KOS_OP_GLOBALS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., globals_dict]`.
- **Invariants**: Frame has globals namespace.

**Example:** Push frame's globals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | GLOBALS| 0   | 0        | stack: [..., globals_dict] |
| 1     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
