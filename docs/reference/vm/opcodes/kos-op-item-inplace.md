# KOS_OP_ITEM_INPLACE
- **Style**: OPCOUNT (operator selector, value count).
- **Stack**: `[..., obj, key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with each value left to right through the object protocol, store it back, drop every entry. Emitted by **compile/inplace-item** when OBJ is an expression or KEY is computed.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE.c`
