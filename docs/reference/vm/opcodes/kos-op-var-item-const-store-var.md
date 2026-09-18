# KOS_OP_VAR_ITEM_CONST_STORE_VAR
- **Style**: EXTRA (VAR, CONST, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_VAR.c`
