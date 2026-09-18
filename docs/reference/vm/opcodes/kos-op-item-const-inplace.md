# KOS_OP_ITEM_CONST_INPLACE
- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE.c`
