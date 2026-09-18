# KOS_OP_VAR_ITEM_CLEAR
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [...]`. Delete obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/del-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CLEAR.c`
