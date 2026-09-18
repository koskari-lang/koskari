# KOS_OP_ITEM_CONST
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST.c`
