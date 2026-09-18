# KOS_OP_ITEM_STORE_VAR
- **Style**: VAR (the value's name).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR value, ITEM_STORE`. May lower to **ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_VAR.c`
