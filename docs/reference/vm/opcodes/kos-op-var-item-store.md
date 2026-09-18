# KOS_OP_VAR_ITEM_STORE
- **Style**: VAR (the object's name).
- **Stack**: `[..., key, value] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE.c`
