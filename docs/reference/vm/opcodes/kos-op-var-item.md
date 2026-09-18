# KOS_OP_VAR_ITEM
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object and the key both resolved by name in the frame namespace. Equivalent to ITEM with nothing on the stack. May lower to **FAST_ITEM**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM.c`
