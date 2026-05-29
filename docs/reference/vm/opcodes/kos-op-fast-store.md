# KOS_OP_FAST_STORE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. **STORE**: store this value in the given
  slot (through the reference that already resolves it). See §1.9. Not
  interchangeable with FAST_BIND.
- **Invariants**: Slot must already be bound. Slot index in range.

**Example:** Store value 99 in slot 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (99)   | stack: [..., 99] |
| 1     | FAST_STORE | 0   | 0 (a)    | STORE: write 99 into slot 0; stack: [...] |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
