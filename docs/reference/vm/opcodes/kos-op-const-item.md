# KOS_OP_CONST_ITEM

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of key (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, value]`. Get item with constant key.
- **Invariants**: TOS is object; key from pool.
- **Compiler form:** Pre-compile arg = key value (e.g. string or index); post-compile arg = pool index.

**Example:** Get obj["key"]. Pool: 0 = "obj", 1 = "key".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM | 1   | 1 ('key')| stack: [..., obj, obj['key']] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
