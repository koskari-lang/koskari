# KOS_OP_VAR_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, NOARG, NOARG).
- **Stack**: `[..., key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE.c`
