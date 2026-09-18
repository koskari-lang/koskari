# KOS_OP_VAR_ITEM_CONST_CLEAR
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM_CLEAR`. May lower to **FAST_ITEM_CONST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_CLEAR.c`
