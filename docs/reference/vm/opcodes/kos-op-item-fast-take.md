# KOS_OP_ITEM_FAST_TAKE
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_TAKE.c`
