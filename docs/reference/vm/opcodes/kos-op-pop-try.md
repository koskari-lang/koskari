# KOS_OP_POP_TRY

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Argument unused (or 0).
- **Status**: Implemented.
- **Stack**: Promote one value (TOS) from try block to parent via PopBlock(1), then drop the block and pop the region. Block must have exactly one value. Clear exception state before pop.
- **Invariants**: Top of chain must be KOS_DYN_TRY.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
