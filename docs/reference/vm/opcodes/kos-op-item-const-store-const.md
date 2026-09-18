# KOS_OP_ITEM_CONST_STORE_CONST
- **Style**: EXTRA (CONST, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Folded by the compress pipeline from `CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_CONST.c`
