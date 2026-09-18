# KOS_OP_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, key] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Folded by the compress pipeline from `CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE_CONST.c`
