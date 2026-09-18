# KOS_OP_VAR_ITEM_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM`. May lower to **FAST_ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR.c`
