# KOS_OP_COMBINE_ARGS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = count of sequences to combine.
- **Status**: Implemented.
- **Stack**: `[..., t_0, ..., t_{m-1}] → [..., tuple]`. Concatenate the m sequences (each via iteration) into a single tuple. Non-tuple values are converted with PySequence_Tuple when m == 1.

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
