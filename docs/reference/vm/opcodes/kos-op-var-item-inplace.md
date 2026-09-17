# KOS_OP_VAR_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, VAR, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with each value left to right, the object and key resolved by name in the frame namespace after the values were pushed. Emitted by **compile/inplace-item** when OBJ is a symbol and KEY is a symbol. May lower to **FAST_ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE.c`
