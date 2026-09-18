# KOS_OP_ITEM_FAST_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE.c`
