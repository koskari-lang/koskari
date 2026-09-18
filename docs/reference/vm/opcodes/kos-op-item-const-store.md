# KOS_OP_ITEM_CONST_STORE
- **Style**: CONST (the key).
- **Stack**: `[..., obj, value] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see the items section of the VM reference).
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE.c`
