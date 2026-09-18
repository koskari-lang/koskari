# KOS_OP_VAR_ITEM_CONST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **VAR_ITEM_CONST_INPLACE**. May lower to **FAST_ITEM_CONST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE_CONST.c`
