# KOS_OP_VAR_CLEAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: No stack effect. Delete name from namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Delete name "tmp". Pool: 0 = "tmp".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | VAR_CLEAR | 0   | 0 ('tmp')| delete "tmp" from namespace |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
