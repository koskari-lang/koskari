# KOS_OP_FAST_ITEM_STORE
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key, value] → [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **VAR_ITEM_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE.c`
