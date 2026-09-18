# KOS_OP_VAR_ITEM_CONST_STORE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object resolved by name, the key a pool constant and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE.c`
