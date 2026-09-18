# KOS_OP_FAST_ITEM_FAST_REF
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_REF.c`
