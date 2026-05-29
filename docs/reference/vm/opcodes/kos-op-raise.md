# KOS_OP_RAISE

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of values to pop (e.g. 1 for exception instance).
- **Status**: Implemented.
- **Stack**: Pops `arg` values; raises with the popped exception.
- **Invariants**: Popped value(s) form the exception to raise.

**Example:** Raise exception from TOS (arg = 1). Pool: 0 = ValueError("bad").

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (ValueError(...)) | stack: [..., exc] |
| 1     | RAISE  | 1   | 1        | pop 1, raise exc; stack unwound |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
