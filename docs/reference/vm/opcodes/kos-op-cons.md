# KOS_OP_CONS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Status**: Implemented.
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result =
  (e_0 . (e_1 . ... (e_{count-1}) ...)). Implemented via KosStackBlock_TailPtr,
  KosPair_ConsFromPtr, KosStackBlock_Drop, then push result. count 0 → push
  nil (INCREF).

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
