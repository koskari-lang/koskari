# KOS_OP_VAR_ITEM_STORE_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_CONST.c`
