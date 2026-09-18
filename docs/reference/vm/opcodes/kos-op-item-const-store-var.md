# KOS_OP_ITEM_CONST_STORE_VAR
- **Style**: EXTRA (CONST, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_CONST_STORE**. May lower to **ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_VAR.c`
