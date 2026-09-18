# KOS_OP_VAR_CONST_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with each value left to right, the object resolved by name after the values were pushed and the key a pool constant. Emitted by **compile/inplace-item** when OBJ is a symbol and KEY is constant. May lower to **FAST_CONST_ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_CONST_ITEM_INPLACE.c`
