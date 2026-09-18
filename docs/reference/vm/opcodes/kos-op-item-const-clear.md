# KOS_OP_ITEM_CONST_CLEAR
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [...]`. Delete obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_CLEAR`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_CLEAR.c`
