# KOS_OP_VAR_CONST_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant, the object resolved by name and the key a pool constant. Squashed from `CONST rhs; VAR_CONST_ITEM_INPLACE` at a count of one. May lower to **FAST_CONST_ITEM_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_CONST_ITEM_INPLACE_CONST.c`
