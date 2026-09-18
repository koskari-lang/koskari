# KOS_OP_ITEM_VAR_STORE_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_CONST.c`
