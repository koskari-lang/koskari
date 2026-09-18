# KOS_OP_ITEM_CLEAR
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [...]`. Delete obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/del-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_CLEAR.c`
