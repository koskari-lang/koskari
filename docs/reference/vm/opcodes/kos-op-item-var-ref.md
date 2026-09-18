# KOS_OP_ITEM_VAR_REF
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_REF`. May lower to **ITEM_FAST_REF**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_REF.c`
