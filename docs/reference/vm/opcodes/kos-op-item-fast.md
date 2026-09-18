# KOS_OP_ITEM_FAST
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **ITEM_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST.c`
