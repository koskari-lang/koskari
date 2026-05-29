# KOS_OP_OBJECT_LT
- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 < a2 < ... < aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_LT.c`

