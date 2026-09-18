# KOS_OP_ITEM_CONST_STORE_FAST
- **Style**: EXTRA (CONST, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **ITEM_CONST_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_FAST.c`
