# KOS_OP_FAST_ITEM_FAST_STORE_CONST
- **Style**: EXTRA (FASTVAR, FASTVAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE_CONST.c`
