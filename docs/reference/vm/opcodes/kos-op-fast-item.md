# KOS_OP_FAST_ITEM
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object and the key both read from fast slots. Equivalent to ITEM with nothing on the stack.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects a body for the container kind it first sees (exact list or tuple under an exact int key, exact dict under any key) and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM.c`
