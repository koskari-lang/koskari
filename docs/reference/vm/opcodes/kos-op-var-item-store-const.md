# KOS_OP_VAR_ITEM_STORE_CONST
- **Style**: EXTRA (VAR, VAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object and the key resolved by name in the frame namespace and the value a pool constant. Squashed from `CONST value; VAR_ITEM_STORE`. May lower to **FAST_ITEM_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_CONST.c`
