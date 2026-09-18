# KOS_OP_VAR_ITEM_CONST_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE.c`
