# KOS_OP_ITEM_VAR
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM`. May lower to **ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR.c`
