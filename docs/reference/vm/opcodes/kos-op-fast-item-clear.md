# KOS_OP_FAST_ITEM_CLEAR
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [...]`. Delete obj[key] with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CLEAR.c`
