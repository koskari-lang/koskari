# KOS_OP_FAST_CONST_ITEM_STORE
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ELECTOR slot; the site elects a body for the container kind it first sees (exact list or tuple under an exact int key, exact dict under any key) and otherwise runs the object protocol.
- **Implementation**: `koskari/lib/opcode/FAST_CONST_ITEM_STORE.c`
