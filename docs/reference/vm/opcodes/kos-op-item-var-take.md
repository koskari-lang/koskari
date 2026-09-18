# KOS_OP_ITEM_VAR_TAKE
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_TAKE`. May lower to **ITEM_FAST_TAKE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_TAKE.c`
