# KOS_OP_CHECK_IS_INSTANCE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL (or COUNT for type tuple). Arg = target pc.
- **Status**: Implemented.
- **Stack**: `[..., obj, type]` (or obj + N types). Pops operands; if isinstance(obj, type) fails, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.

**Example:** Pattern match by type. Pool: 0 = obj, 1 = ValueError.

| index | opcode          | arg | arg view | comment |
|------:|-----------------|-----|----------|---------|
| 0     | EXCEPTION       | 0   | 0        | stack: [..., exc] |
| 1     | CONST           | 0   | 0 (ValueError) | stack: [..., exc, typ] |
| 2     | CHECK_IS_INSTANCE | 5  | 5 (-> 5) | if not isinstance(exc, typ) → pc=5 |
| 3     | RETURN          | 1   | 1        | handle |
| 5     | LABEL           | —   | —        | re-raise |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
