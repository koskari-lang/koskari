# KOS_OP_FAST_CONST_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with each value left to right, the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects its operator's int or float body for the item and operand it first sees, finds the cell by an exact list or dict check inside the body, and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_CONST_ITEM_INPLACE.c`
