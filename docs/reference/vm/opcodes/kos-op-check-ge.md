# KOS_OP_CHECK_GE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if **not** `left >= right`, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
