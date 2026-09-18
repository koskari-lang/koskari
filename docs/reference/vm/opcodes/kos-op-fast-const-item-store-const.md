# KOS_OP_FAST_CONST_ITEM_STORE_CONST
- **Style**: EXTRA (FASTVAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot and the key and value pool constants.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects a body for the container kind it first sees (exact list under an exact int key, exact dict under any key) and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_CONST_ITEM_STORE_CONST.c`
