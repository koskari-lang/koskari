# KOS_OP_ITEM_CLEAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Key from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj]`. Delete item.
- **Invariants**: Object and key on stack.

**Example:** del obj[key].

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST      | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM_CLEAR | 0   | 0        | del d['k']; stack: [..., d] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
