# KOS_OP_OBJECT_INPLACE_POWER
- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Arg = 2, strictly binary (no reduce). Pop exp then base, push `PyNumber_InPlacePower(base, exp, Py_None)` result or await it. The in-place `**` used by the inplace-item and inplace-attr special forms; the ternary pow(base, exp, mod) has no in-place form.
- **Implementation**: `koskari/lib/opcode/OBJECT_INPLACE_POWER.c`

