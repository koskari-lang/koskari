# KOS_OP_SWITCH_EQ_VAR_ATTR
- **Style**: EXTRA. Schema `(LABEL, VAR, CONST, NOARG)`.
- **Stack**: `[..., s]`. Resolve `obj = frame_ns_deref(var_name)` from `a1`,
  `rhs = getattr(obj, attr_name)` from `a2`, then compare `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "obj_name", "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR_ATTR.c`

