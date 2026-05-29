# KOS_OP_ATTR_STORE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj, value] → [..., obj]`. Pop value, set attribute on obj.
- **Invariants**: Below TOS is object; name in pool.

**Example:** Set obj.x = TOS. Pool: 0 = "x".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST      | 1   | 1 (10)   | stack: [..., obj, 10] |
| 2     | ATTR_STORE | 2   | 2 ('x')  | obj.x = 10; stack: [..., obj] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
