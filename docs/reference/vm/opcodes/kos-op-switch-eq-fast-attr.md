# KOS_OP_SWITCH_EQ_FAST_ATTR
- **Style**: EXTRA. Schema `(LABEL, FASTVAR, CONST, NOARG)`.
- **Stack**: `[..., s]`. Resolve `obj = KosNamespace_FastDeref(ns, a1)`,
  `rhs = getattr(obj, attr_name)` from `a2`, then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST_ATTR.c`


## 10. Introspection

