# KOS_OP_ITEM_VAR_STORE
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj, value] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **ITEM_FAST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE.c`
