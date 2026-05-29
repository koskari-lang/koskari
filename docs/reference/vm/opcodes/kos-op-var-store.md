# KOS_OP_VAR_STORE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. **STORE**: store this value in the given
  name (through the reference that already resolves it). See §1.9.
- **Invariants**: Name must already be bound.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Store value 10 into name "x". Pool: 0 = "x".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (10)   | stack: [..., 10] |
| 1     | VAR_STORE | 1   | 1 ('x')  | STORE: write 10 into name "x"; stack: [...] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
