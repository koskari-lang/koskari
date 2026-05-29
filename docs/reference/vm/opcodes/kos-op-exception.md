# KOS_OP_EXCEPTION

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., exception]`. Push current exception (in handler).
- **Invariants**: Used when an exception is being handled.

**Example:** In except block: load current exception.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 2     | EXCEPTION| 0   | 0        | stack: [..., exc] |
| 3     | VAR_STORE| 0   | 0 (e)    | store exc in name "e" |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
