# KOS_OP_SWITCH_EQ_FAST
- **Style**: EXTRA. Schema `(LABEL, FASTVAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Resolve `rhs = KosNamespace_FastDeref(ns, a1)`, then
  compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST.c`

