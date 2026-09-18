# KOS_OP_VAR_CONST_ITEM_STORE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object resolved by name in the frame namespace after the value was pushed and the key a pool constant. Emitted by **compile/set-item** when OBJ is a symbol and KEY is a constant. May lower to **FAST_CONST_ITEM_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_CONST_ITEM_STORE.c`
