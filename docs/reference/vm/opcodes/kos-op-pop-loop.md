# KOS_OP_POP_LOOP

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Drop stack block (no promotion); pop LOOP region; push loop result from the region (stored on the region, not from the block).
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
