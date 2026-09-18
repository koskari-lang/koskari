# KOS_OP_VAR_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., key] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST operand` ahead of **VAR_ITEM_INPLACE** at a count of one, the row **`compile/inplace-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE_CONST.c`
