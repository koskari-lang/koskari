# KOS_OP_VAR_ITEM_VAR_STORE_VAR
- **Style**: EXTRA (VAR, VAR, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_VAR.c`
