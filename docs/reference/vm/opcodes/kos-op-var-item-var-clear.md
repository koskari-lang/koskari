# KOS_OP_VAR_ITEM_VAR_CLEAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM_CLEAR`. May lower to **FAST_ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_CLEAR.c`
