# KOS_OP_FAST_REF

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [..., ref]`. Push reference to slot.
- **Invariants**: Slot index in range.

**Example:** Push reference to slot 0.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | FAST_REF| 0   | 0 (a)    | stack: [..., ref] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
