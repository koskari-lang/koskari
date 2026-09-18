# KOS_OP_VAR_ITEM_VAR_STORE_CONST
- **Style**: EXTRA (VAR, VAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_CONST.c`
