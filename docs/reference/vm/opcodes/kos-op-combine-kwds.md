# KOS_OP_COMBINE_KWDS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = count of dicts to merge.
- **Status**: Implemented.
- **Stack**: `[..., d_0, ..., d_{m-1}] → [..., dict]`. Merge the m dicts (later overwrites earlier on key clash) and push result.


## 17. Calls

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
