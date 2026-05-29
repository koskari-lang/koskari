# KOS_OP_FAST_ATTR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object in fast slot. Equivalent to ATTR but object from slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR.c`

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
