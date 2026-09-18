# KOS_OP_SWITCH_EQ_VAR
- **Style**: EXTRA. Schema `(LABEL, VAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Resolve `rhs = frame_ns_deref(var_name)` from `a1`,
  then compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "name", 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR.c`

