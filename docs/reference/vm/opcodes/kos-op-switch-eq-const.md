# KOS_OP_SWITCH_EQ_CONST
- **Style**: EXTRA. Schema `(LABEL, CONST, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Compare `s == cpool[a1]` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged (`s` still TOS).
- **Compiler form:** tuple arg `("label", const_value, 0, 0)` pre-compile;
  post-compile row stores pc in `a0` and constant index in `a1`.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_CONST.c`

