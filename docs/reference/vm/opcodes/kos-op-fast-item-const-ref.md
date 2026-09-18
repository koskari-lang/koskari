# KOS_OP_FAST_ITEM_CONST_REF
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_REF.c`
