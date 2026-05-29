# KOS_OP_CALL_VAR_STAR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: VAR (name cpool idx), STAR (bitflags: 1 = *args, 2 = **kwargs), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_STAR minus callable. `[..., pos, kwd]` or subsets per flags. Callable from namespace by name. Pop per flags, ensure pos is tuple, call via KosContext_CallArgs.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
