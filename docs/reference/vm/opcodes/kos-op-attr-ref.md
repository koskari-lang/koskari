# KOS_OP_ATTR_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, attr_ref]`. Push attribute reference.
- **Invariants**: TOS is the object.

**Example:** Push reference for obj.prop. Pool: 0 = "prop".

| index | opcode   | arg | arg view  | comment |
|------:|----------|-----|-----------|---------|
| 0     | VAR      | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_REF | 1   | 1 ('prop')| stack: [..., obj, attr_ref] |


## 15. Items

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
