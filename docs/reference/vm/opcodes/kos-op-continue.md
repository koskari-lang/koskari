# KOS_OP_CONTINUE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Status**: Implemented.
- **Stack**: Pops `arg` elements; control transfers to continue target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent continue_pc (e.g. loop head).

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 4     | CONTINUE| 0   | 0        | pop 0, jump to continue target (e.g. pc 1) |
| …     | …       | …   | …        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
