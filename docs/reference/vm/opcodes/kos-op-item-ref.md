# KOS_OP_ITEM_REF
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/ref-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_REF.c`
