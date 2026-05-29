# KOS_OP_CONST_ITEM_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, item_ref]`. Push item reference for constant key.
- **Invariants**: Key from pool.

**Example:** Push reference for obj["key"]. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM_REF| 0   | 0 ('key')| stack: [..., obj, item_ref] |


## 15.1 Reference dereference

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
