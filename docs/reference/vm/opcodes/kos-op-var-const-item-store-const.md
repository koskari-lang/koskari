# KOS_OP_VAR_CONST_ITEM_STORE_CONST
- **Style**: EXTRA (VAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name in the frame namespace and the key and value pool constants. Squashed from `CONST value; VAR_CONST_ITEM_STORE`. May lower to **FAST_CONST_ITEM_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_CONST_ITEM_STORE_CONST.c`
