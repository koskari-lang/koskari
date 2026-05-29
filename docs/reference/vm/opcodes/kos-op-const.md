# KOS_OP_CONST

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., c]` where `c = cpool[arg]`.
- **Invariants**: `arg < cpool_len`.
- **Compiler form:** Pre-compile arg = literal value (e.g. `CONST "hello"`, `CONST
  42`). Finalize accumulates values into the constant pool and rewrites arg to
  the pool index.

**Example:** Load constant at index 0 (value 42). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | |

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
