# KOS_OP_VAR_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., ref]`. Push a reference to the named variable (e.g. for closure).
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Push reference for "cell". Pool: 0 = "cell".

| index | opcode | arg | arg view  | comment |
|------:|--------|-----|-----------|---------|
| 0     | VAR_REF| 0   | 0 ('cell')| stack: [..., ref] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
