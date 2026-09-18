# KOS_OP_ITEM_CONST_REF
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_REF`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_REF.c`
