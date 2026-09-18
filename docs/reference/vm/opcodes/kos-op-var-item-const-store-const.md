# KOS_OP_VAR_ITEM_CONST_STORE_CONST
- **Style**: EXTRA (VAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_CONST.c`
