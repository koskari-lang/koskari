# KOS_OP_FAST_ITEM_STORE_CONST
- **Style**: EXTRA (FASTVAR, FASTVAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object and the key read from fast slots and the value a pool constant.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects a body for the container kind it first sees (exact list under an exact int key, exact dict under any key) and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE_CONST.c`
