# KOS_OP_SWITCH_EQ_CONST

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema `(LABEL, CONST, NOARG, NOARG)`.
- **Status**: Implemented.
- **Stack**: `[..., s]`. Compare `s == cpool[a1]` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged (`s` still TOS).
- **Compiler form:** tuple arg `("label", const_value, 0, 0)` pre-compile;
  post-compile row stores pc in `a0` and constant index in `a1`.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_CONST.c`

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
