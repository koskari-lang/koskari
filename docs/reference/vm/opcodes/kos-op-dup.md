# KOS_OP_DUP

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of elements to duplicate.
- **Status**: Implemented.
- **Stack**: `[..., a_1, ..., a_n] → [..., a_1, ..., a_n, a_1, ..., a_n]` (top `arg` elements duplicated).
- **Invariants**: Stack must have at least `arg` elements.

**Example:** DUP 1: duplicate TOS (e.g. for use in two branches).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (10)   | stack: [..., 10] |
| 1     | DUP    | 1   | 1        | stack: [..., 10, 10] |
| 2     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
