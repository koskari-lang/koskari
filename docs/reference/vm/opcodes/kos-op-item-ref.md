# KOS_OP_ITEM_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Key from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj, item_ref]`. Push item reference.
- **Invariants**: Object and key on stack.

**Example:** Push reference for d[k]. Pool: 0 = "d", 1 = "k".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | VAR      | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST    | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM_REF | 0   | 0        | stack: [..., d, item_ref] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
