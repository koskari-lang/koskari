# KOS_OP_ITEM_STORE
- **Style**: NOARG.
- **Stack**: `[..., obj, key, value] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Emitted by **`compile/set-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE.c`
