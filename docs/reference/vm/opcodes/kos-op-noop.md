# KOS_OP_NOOP

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: No change.
- **Invariants**: None beyond global.
- **Behavior**: Advance PC only; no effect.

**Example:** NOOP in a short sequence (e.g. after label resolution).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | NOOP   | 0   | 0        | no effect; advance PC |
| 2     | RETURN | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
