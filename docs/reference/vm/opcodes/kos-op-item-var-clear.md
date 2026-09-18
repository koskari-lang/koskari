# KOS_OP_ITEM_VAR_CLEAR
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [...]`. Delete obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_CLEAR`. May lower to **ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_CLEAR.c`
