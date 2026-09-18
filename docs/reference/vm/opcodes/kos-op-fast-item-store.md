# KOS_OP_FAST_ITEM_STORE
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object and the key read from fast slots.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects a body for the container kind it first sees (exact list or tuple under an exact int key, exact dict under any key) and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE.c`
