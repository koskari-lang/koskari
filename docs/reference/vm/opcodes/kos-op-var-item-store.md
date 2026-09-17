# KOS_OP_VAR_ITEM_STORE
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object and the key resolved by name in the frame namespace after the value was pushed. Emitted by **compile/set-item** when OBJ is a symbol and KEY is a symbol. May lower to **FAST_ITEM_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE.c`
