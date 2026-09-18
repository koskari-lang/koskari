# KOS_OP_FAST_ITEM_FAST_TAKE
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_TAKE.c`
