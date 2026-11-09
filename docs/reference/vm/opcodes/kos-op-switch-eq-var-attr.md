# KOS_OP_SWITCH_EQ_VAR_ATTR
- **Style**: EXTRA. Schema `(LABEL, VAR, CONST, NOARG)`.
- **Stack**: `[..., s]`. Read `obj` from the frame namespace by the name in `a1`, then
  `rhs = getattr(obj, attr_name)` with the name in `a2`, then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "obj_name", "attr_name", 0)` pre-compile.

