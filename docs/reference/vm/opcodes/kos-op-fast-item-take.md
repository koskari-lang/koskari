# KOS_OP_FAST_ITEM_TAKE
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_TAKE.c`
