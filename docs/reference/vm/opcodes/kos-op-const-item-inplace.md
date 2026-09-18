# KOS_OP_CONST_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with each value left to right, the key a pool constant, store it back, drop every entry. Emitted by **compile/inplace-item** when OBJ is an expression and KEY is constant.
- **Implementation**: `koskari/lib/opcode/CONST_ITEM_INPLACE.c`
