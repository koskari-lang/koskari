# KOS_OP_ITEM_STORE_CONST
- **Style**: CONST (the value).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Folded by the compress pipeline from `CONST value, ITEM_STORE`.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_CONST.c`
