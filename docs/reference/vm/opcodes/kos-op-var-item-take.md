# KOS_OP_VAR_ITEM_TAKE
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/take-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_TAKE.c`
