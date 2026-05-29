# KOS_OP_OBJECT_EQ

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 == a2 == ... == aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_EQ.c`

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
