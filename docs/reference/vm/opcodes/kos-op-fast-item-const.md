# KOS_OP_FAST_ITEM_CONST
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **VAR_ITEM_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST.c`
