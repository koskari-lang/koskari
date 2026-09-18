# KOS_OP_ITEM_INPLACE
- **Style**: OPCOUNT (operator selector, value count).
- **Stack**: `[..., obj, key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Emitted by **`compile/inplace-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE.c`
