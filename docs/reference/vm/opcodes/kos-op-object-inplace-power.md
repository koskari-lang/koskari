# KOS_OP_OBJECT_INPLACE_POWER
- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: Pop rhs then lhs, push `PyNumber_InPlacePower(lhs, rhs, Py_None)` result or await it. The in-place `**` used by the inplace-item and inplace-attr special forms.
- **Implementation**: `koskari/lib/opcode/OBJECT_INPLACE_POWER.c`

