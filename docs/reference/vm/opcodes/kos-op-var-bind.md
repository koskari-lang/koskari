# KOS_OP_VAR_BIND

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., ref] → [...]`. **BIND**: use this reference to resolve the
  given name in the frame namespace. See §1.9.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); post-compile arg = pool index.

**Example:** Bind name "n" to the reference at TOS. Pool: 0 = "n".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (42)   | stack: [..., ref] |
| 1     | VAR_BIND | 1   | 1 ('n')  | BIND: name "n" resolves to this ref; stack: [...] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
