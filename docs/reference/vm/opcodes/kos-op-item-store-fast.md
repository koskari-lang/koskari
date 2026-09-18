# KOS_OP_ITEM_STORE_FAST
- **Style**: FASTVAR (the value's slot).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **ITEM_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_FAST.c`
