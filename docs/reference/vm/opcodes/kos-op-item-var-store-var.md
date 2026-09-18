# KOS_OP_ITEM_VAR_STORE_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_VAR.c`
