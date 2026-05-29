# KOS_OP_ITEM

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Key from stack (below object).
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj, value]`. Get item; push value.
- **Invariants**: TOS = key, below = object.

**Example:** Get obj[key]. Pool: 0 = "d", 1 = "k".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST  | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM   | 0   | 0        | stack: [..., d, d['k']] |
| 3     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
