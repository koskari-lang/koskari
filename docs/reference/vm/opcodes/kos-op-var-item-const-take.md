# KOS_OP_VAR_ITEM_CONST_TAKE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM_TAKE`. May lower to **FAST_ITEM_CONST_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_TAKE.c`
