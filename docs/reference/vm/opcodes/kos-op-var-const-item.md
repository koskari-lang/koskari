# KOS_OP_VAR_CONST_ITEM
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object resolved by name in the frame namespace and the key a pool constant. Equivalent to CONST_ITEM with nothing on the stack. May lower to **FAST_CONST_ITEM**.
- **Implementation**: `koskari/lib/opcode/VAR_CONST_ITEM.c`
