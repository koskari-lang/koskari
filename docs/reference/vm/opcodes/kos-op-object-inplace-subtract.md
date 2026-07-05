# KOS_OP_OBJECT_INPLACE_SUBTRACT
- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Arg = number of operands `N` (`N >= 2`). Pop N values, reduce left-to-right via `PyNumber_InPlaceSubtract`, push the result or await it. The in-place `-` used by the inplace-item and inplace-attr special forms.
- **Implementation**: `koskari/lib/opcode/OBJECT_INPLACE_SUBTRACT.c`

