# KOS_OP_ITEM
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Get obj[key] with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Emitted by **`compile/item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM.c`
