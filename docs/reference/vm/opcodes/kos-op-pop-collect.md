# KOS_OP_POP_COLLECT

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: Verifies dyn_top is KOS_DYN_COLLECT. Restores frame ns, drops the stack
  block (discarding body expression results), pops the dyn node, **pushes the
  collected namespace** onto the value stack. `[..., pos] → [..., pos, ns]`.
- **Invariants**: Top of chain must be KOS_DYN_COLLECT.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
