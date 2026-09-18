# KOS_OP_ITEM_TAKE
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/take-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_TAKE.c`
