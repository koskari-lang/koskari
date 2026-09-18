# KOS_OP_ITEM_FAST_REF
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_REF.c`
