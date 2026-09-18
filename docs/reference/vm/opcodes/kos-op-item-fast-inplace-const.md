# KOS_OP_ITEM_FAST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE_CONST.c`
