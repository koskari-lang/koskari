# KOS_OP_POP_PARAM

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No change.
- **Pops:** KOS_DYN_PARAM from dynamic chain. Restores domain param bindings.
- **Invariants**: Dyn top must be KOS_DYN_PARAM; otherwise RuntimeError.
- **Example:** See PUSH_PARAM table.


## 24. Sentinel

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
