# KOS_OP_VAR_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, VAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant, the object and key resolved by name. Squashed from `CONST rhs; VAR_ITEM_INPLACE` at a count of one. May lower to **FAST_ITEM_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE_CONST.c`
