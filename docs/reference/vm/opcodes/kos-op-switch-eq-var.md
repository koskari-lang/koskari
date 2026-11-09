# KOS_OP_SWITCH_EQ_VAR
- **Style**: EXTRA. Schema `(LABEL, VAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Read `rhs` from the frame namespace by the name in `a1`,
  then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "name", 0, 0)` pre-compile.

