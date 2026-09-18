# KOS_OP_VAR_ITEM_STORE_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_VAR.c`
