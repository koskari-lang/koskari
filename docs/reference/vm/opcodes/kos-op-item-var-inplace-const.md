# KOS_OP_ITEM_VAR_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_VAR_INPLACE**. May lower to **ITEM_FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_INPLACE_CONST.c`
