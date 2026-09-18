# KOS_OP_ITEM_CONST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, CONST, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Folded by the compress pipeline from `CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE_CONST.c`
