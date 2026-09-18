# KOS_OP_FAST_ITEM_CONST_CLEAR
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_CLEAR.c`
